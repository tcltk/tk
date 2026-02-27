/*
 *
 *  tkWaylandDecor.c – 
 * 
 * Client-side window decorations for Tcl/Tk on Wayland/GLFW using NanoVG.
 * Includes policy management for CSD/SSD priority and automatic detection. 
 * Incorporates code from the libdecor project.
 * 
 *  Copyright © 2017-2018 Red Hat Inc.
 *  Copyright © 2018 Jonas Ådahl
 *  Copyright © 2026 Kevin Walzer
 *
 */

#include "tkInt.h"
#include "tkGlfwInt.h"
#include <GLFW/glfw3.h>
#include <nanovg.h>
#include <string.h>
#include <stdlib.h>
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"
#include "xdg-decoration-client-protocol.h"

/* Decoration modes. */
typedef enum {
    DECOR_AUTO,         /* Prefer SSD, fallback to CSD */
    DECOR_SERVER_ONLY,  /* SSD only */
    DECOR_CLIENT_ONLY,  /* CSD only */
    DECOR_NONE          /* No decorations */
} TkWaylandDecorMode;

static TkWaylandDecorMode decorationMode = DECOR_AUTO;
static int ssdAvailable = 0;

/* Forward declarations. */
static void DrawTitleBar(NVGcontext *vg, TkWaylandDecoration *decor, int width, int height);
static void DrawBorder(NVGcontext *vg, TkWaylandDecoration *decor, int width, int height);
static void DrawButton(NVGcontext *vg, ButtonType type, ButtonState state, float x, float y, float w, float h);
static void HandleButtonClick(TkWaylandDecoration *decor, ButtonType button);
static int GetButtonAtPosition(TkWaylandDecoration *decor, double x, double y, int width);
static int GetResizeEdge(double x, double y, int width, int height);
static void UpdateButtonStates(TkWaylandDecoration *decor, double x, double y, int width);
static void TkWaylandConfigureCallback(void *data, int width, int height);
static void TkWaylandCloseCallback(void *data);
MODULE_SCOPE TkWaylandPlatformInfo *TkGetWaylandPlatformInfo(void);

/* Wayland window management function implementations. */
static void xdg_surface_configure_handler(void *data,
                                          struct xdg_surface *xdg_surface,
                                          uint32_t serial);
static void xdg_toplevel_configure_handler(void *data,
                                           struct xdg_toplevel *xdg_toplevel,
                                           int32_t width, int32_t height,
                                           struct wl_array *states);
static void xdg_toplevel_close_handler(void *data,
                                       struct xdg_toplevel *xdg_toplevel);
static void toplevel_decoration_configure_handler(void *data,
                                                  struct zxdg_toplevel_decoration_v1 *decoration,
                                                  uint32_t mode);

/* XDG Shell listeners. */
static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure_handler,
};

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_configure_handler,
    .close = xdg_toplevel_close_handler,
};

static const struct zxdg_toplevel_decoration_v1_listener toplevel_decoration_listener = {
    .configure = toplevel_decoration_configure_handler,
};

/* XDG WM Base listener. */
static void xdg_wm_base_ping_handler(TCL_UNUSED(void *),  /* data */
                                     struct xdg_wm_base *xdg_wm_base,
                                     uint32_t serial)
{
    xdg_wm_base_pong(xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = xdg_wm_base_ping_handler,
};

/* Registry listeners. */
static void registry_global_handler(void *data,
                                    struct wl_registry *registry,
                                    uint32_t name,
                                    const char *interface,
                                    uint32_t version)
{
    TkWaylandWmContext *ctx = (TkWaylandWmContext *)data;
    
    if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        ctx->xdg_wm_base = wl_registry_bind(registry, name,
                                            &xdg_wm_base_interface,
                                            version < 2 ? version : 2);
        xdg_wm_base_add_listener(ctx->xdg_wm_base, &xdg_wm_base_listener, ctx);
    } else if (strcmp(interface, zxdg_decoration_manager_v1_interface.name) == 0) {
        ctx->decoration_manager = wl_registry_bind(registry, name,
                                                   &zxdg_decoration_manager_v1_interface,
                                                   version < 2 ? version : 2);
    }
}

static void registry_global_remove_handler(TCL_UNUSED(void *),  /* data */
                                           TCL_UNUSED(struct wl_registry *), /* registry */
                                           TCL_UNUSED(uint32_t)) /* name */
{
    /* Nothing to do. */
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global_handler,
    .global_remove = registry_global_remove_handler
};

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandResizeEdgeFromInt --
 *
 *      Convert from our internal RESIZE_* bitmask to TkWaylandResizeEdge enum.
 *
 * Results:
 *      TkWaylandResizeEdge value.
 *
 *----------------------------------------------------------------------
 */

TkWaylandResizeEdge
TkWaylandResizeEdgeFromInt(int edge)
{
    switch (edge) {
    case RESIZE_NONE:      return TK_WAYLAND_RESIZE_EDGE_NONE;
    case RESIZE_TOP:       return TK_WAYLAND_RESIZE_EDGE_TOP;
    case RESIZE_BOTTOM:    return TK_WAYLAND_RESIZE_EDGE_BOTTOM;
    case RESIZE_LEFT:      return TK_WAYLAND_RESIZE_EDGE_LEFT;
    case RESIZE_TOP | RESIZE_LEFT:     return TK_WAYLAND_RESIZE_EDGE_TOP_LEFT;
    case RESIZE_TOP | RESIZE_RIGHT:    return TK_WAYLAND_RESIZE_EDGE_TOP_RIGHT;
    case RESIZE_BOTTOM | RESIZE_LEFT:  return TK_WAYLAND_RESIZE_EDGE_BOTTOM_LEFT;
    case RESIZE_BOTTOM | RESIZE_RIGHT: return TK_WAYLAND_RESIZE_EDGE_BOTTOM_RIGHT;
    default:               return TK_WAYLAND_RESIZE_EDGE_NONE;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Convert from TkWaylandResizeEdge to XDG edge (static inline in header)
 *
 *----------------------------------------------------------------------
 */

static inline enum xdg_toplevel_resize_edge
TkWaylandResizeEdgeToXdg(TkWaylandResizeEdge edge)
{
    switch (edge) {
    case TK_WAYLAND_RESIZE_EDGE_NONE:
        return XDG_TOPLEVEL_RESIZE_EDGE_NONE;
    case TK_WAYLAND_RESIZE_EDGE_TOP:
        return XDG_TOPLEVEL_RESIZE_EDGE_TOP;
    case TK_WAYLAND_RESIZE_EDGE_BOTTOM:
        return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM;
    case TK_WAYLAND_RESIZE_EDGE_LEFT:
        return XDG_TOPLEVEL_RESIZE_EDGE_LEFT;
    case TK_WAYLAND_RESIZE_EDGE_TOP_LEFT:
        return XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT;
    case TK_WAYLAND_RESIZE_EDGE_BOTTOM_LEFT:
        return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT;
    case TK_WAYLAND_RESIZE_EDGE_RIGHT:
        return XDG_TOPLEVEL_RESIZE_EDGE_RIGHT;
    case TK_WAYLAND_RESIZE_EDGE_TOP_RIGHT:
        return XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT;
    case TK_WAYLAND_RESIZE_EDGE_BOTTOM_RIGHT:
        return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT;
    }
    return XDG_TOPLEVEL_RESIZE_EDGE_NONE;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandWmCreateContext --
 *
 *      Create a new Wayland window management context.
 *
 * Results:
 *      Pointer to new context, or NULL on failure.
 *----------------------------------------------------------------------
 */

TkWaylandWmContext *
TkWaylandWmCreateContext(struct wl_display *display)
{
    TkWaylandWmContext *ctx;
    
    if (!display) {
        return NULL;
    }
    
    ctx = (TkWaylandWmContext *)calloc(1, sizeof(TkWaylandWmContext));
    if (!ctx) {
        return NULL;
    }
    
    ctx->display = display;
    ctx->ref_count = 1;
    
    ctx->registry = wl_display_get_registry(display);
    if (!ctx->registry) {
        free(ctx);
        return NULL;
    }
    
    wl_registry_add_listener(ctx->registry, &registry_listener, ctx);
    wl_display_roundtrip(display);  /* Ensure we get the globals. */
    
    if (!ctx->xdg_wm_base) {
        /* No xdg_wm_base support - incompatible compositor. */
        wl_registry_destroy(ctx->registry);
        free(ctx);
        return NULL;
    }
    
    return ctx;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandWmDestroyContext --
 *
 *      Destroy a Wayland window management context.
 *----------------------------------------------------------------------
 */

void
TkWaylandWmDestroyContext(TkWaylandWmContext *ctx)
{
    if (!ctx) {
        return;
    }
    
    ctx->ref_count--;
    if (ctx->ref_count > 0) {
        return;
    }
    
    if (ctx->decoration_manager) {
        zxdg_decoration_manager_v1_destroy(ctx->decoration_manager);
    }
    if (ctx->xdg_wm_base) {
        xdg_wm_base_destroy(ctx->xdg_wm_base);
    }
    if (ctx->registry) {
        wl_registry_destroy(ctx->registry);
    }
    
    free(ctx);
}

/*
 *----------------------------------------------------------------------
 *
 * xdg_surface_configure_handler --
 *
 *      Handle xdg_surface configure event.
 *----------------------------------------------------------------------
 */

static void
xdg_surface_configure_handler(void *data,
                              struct xdg_surface *xdg_surface,
                              uint32_t serial)
{
    TkWaylandWmWindow *win = (TkWaylandWmWindow *)data;
    
    /* Acknowledge the configure. */
    xdg_surface_ack_configure(xdg_surface, serial);
    
    /* Call the user's configure callback if this was a size change. */
    if (win->configure_callback && win->content_width > 0 && win->content_height > 0) {
        win->configure_callback(win->user_data, win->content_width, win->content_height);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * parse_window_states --
 *
 *      Parse XDG toplevel states into our internal state.
 *----------------------------------------------------------------------
 */

static void
parse_window_states(TkWaylandWmWindow *win, struct wl_array *states)
{
    uint32_t *state;
    
    win->maximized = 0;
    win->fullscreen = 0;
    
    wl_array_for_each(state, states) {
        switch (*state) {
        case XDG_TOPLEVEL_STATE_MAXIMIZED:
            win->maximized = 1;
            break;
        case XDG_TOPLEVEL_STATE_FULLSCREEN:
            win->fullscreen = 1;
            break;
        default:
            break;
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * xdg_toplevel_configure_handler --
 *
 *      Handle xdg_toplevel configure event.
 *----------------------------------------------------------------------
 */

static void
xdg_toplevel_configure_handler(void *data,
                               TCL_UNUSED(struct xdg_toplevel *), /* xdg_toplevel */
                               int32_t width,
                               int32_t height,
                               struct wl_array *states)
{
    TkWaylandWmWindow *win = (TkWaylandWmWindow *)data;
    
    /* Store the new size. */
    if (width > 0 && height > 0) {
        win->content_width = width;
        win->content_height = height;
    }
    
    /* Parse window states. */
    parse_window_states(win, states);
}

/*
 *----------------------------------------------------------------------
 *
 * xdg_toplevel_close_handler --
 *
 *      Handle xdg_toplevel close event.
 *----------------------------------------------------------------------
 */

static void
xdg_toplevel_close_handler(void *data,
                           TCL_UNUSED(struct xdg_toplevel *)) /* xdg_toplevel */
{
    TkWaylandWmWindow *win = (TkWaylandWmWindow *)data;
    
    if (win->close_callback) {
        win->close_callback(win->user_data);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * toplevel_decoration_configure_handler --
 *
 *      Handle decoration manager configure event.
 *----------------------------------------------------------------------
 */

static void
toplevel_decoration_configure_handler(void *data,
                                      TCL_UNUSED(struct zxdg_toplevel_decoration_v1 *), /* decoration */
                                      uint32_t mode)
{
    TkWaylandWmWindow *win = (TkWaylandWmWindow *)data;
    
    win->decoration_mode = mode;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandWmCreateWindow --
 *
 *      Create a new Wayland-managed window with decorations.
 *----------------------------------------------------------------------
 */

TkWaylandWmWindow *
TkWaylandWmCreateWindow(TkWaylandWmContext *ctx,
                        struct wl_surface *surface,
                        void (*configure)(void*,int,int),
                        void (*close)(void*),
                        void *user_data)
{
    TkWaylandWmWindow *win;
    
    if (!ctx || !surface || !configure) {
        return NULL;
    }
    
    win = (TkWaylandWmWindow *)calloc(1, sizeof(TkWaylandWmWindow));
    if (!win) {
        return NULL;
    }
    
    win->surface = surface;
    win->configure_callback = configure;
    win->close_callback = close;
    win->user_data = user_data;
    win->decoration_mode = ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;
    
    /* Create xdg_surface. */
    win->xdg_surface = xdg_wm_base_get_xdg_surface(ctx->xdg_wm_base, surface);
    if (!win->xdg_surface) {
        free(win);
        return NULL;
    }
    xdg_surface_add_listener(win->xdg_surface, &xdg_surface_listener, win);
    
    /* Create xdg_toplevel. */
    win->xdg_toplevel = xdg_surface_get_toplevel(win->xdg_surface);
    if (!win->xdg_toplevel) {
        xdg_surface_destroy(win->xdg_surface);
        free(win);
        return NULL;
    }
    xdg_toplevel_add_listener(win->xdg_toplevel, &xdg_toplevel_listener, win);
    
    /* Set up decorations if available. */
    if (ctx->decoration_manager) {
        win->toplevel_decoration = 
            zxdg_decoration_manager_v1_get_toplevel_decoration(
                ctx->decoration_manager, win->xdg_toplevel);
        if (win->toplevel_decoration) {
            zxdg_toplevel_decoration_v1_add_listener(
                win->toplevel_decoration,
                &toplevel_decoration_listener,
                win);
        }
    }
    
    return win;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandWmDestroyWindow --
 *
 *      Destroy a Wayland-managed window.
 *----------------------------------------------------------------------
 */

void
TkWaylandWmDestroyWindow(TkWaylandWmWindow *win)
{
    if (!win) {
        return;
    }
    
    if (win->toplevel_decoration) {
        zxdg_toplevel_decoration_v1_destroy(win->toplevel_decoration);
    }
    if (win->xdg_toplevel) {
        xdg_toplevel_destroy(win->xdg_toplevel);
    }
    if (win->xdg_surface) {
        xdg_surface_destroy(win->xdg_surface);
    }
    if (win->title) {
        free(win->title);
    }
    if (win->app_id) {
        free(win->app_id);
    }
    
    free(win);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandWmSetTitle --
 *
 *      Set the window title.
 *----------------------------------------------------------------------
 */

void
TkWaylandWmSetTitle(TkWaylandWmWindow *win, const char *title)
{
    if (!win || !title) {
        return;
    }
    
    free(win->title);
    win->title = strdup(title);
    
    if (win->xdg_toplevel) {
        xdg_toplevel_set_title(win->xdg_toplevel, title);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandWmSetAppId --
 *
 *      Set the application ID.
 *----------------------------------------------------------------------
 */

void
TkWaylandWmSetAppId(TkWaylandWmWindow *win, const char *app_id)
{
    if (!win || !app_id) {
        return;
    }
    
    free(win->app_id);
    win->app_id = strdup(app_id);
    
    if (win->xdg_toplevel) {
        xdg_toplevel_set_app_id(win->xdg_toplevel, app_id);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandWmSetParent --
 *
 *      Set the parent window.
 *----------------------------------------------------------------------
 */

void
TkWaylandWmSetParent(TkWaylandWmWindow *win, TkWaylandWmWindow *parent)
{
    if (!win || !win->xdg_toplevel) {
        return;
    }
    
    xdg_toplevel_set_parent(win->xdg_toplevel, 
                            parent ? parent->xdg_toplevel : NULL);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandWmMove --
 *
 *      Start an interactive window move.
 *----------------------------------------------------------------------
 */

void
TkWaylandWmMove(TkWaylandWmWindow *win, struct wl_seat *seat, uint32_t serial)
{
    if (!win || !win->xdg_toplevel || !seat) {
        return;
    }
    
    xdg_toplevel_move(win->xdg_toplevel, seat, serial);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandWmResize --
 *
 *      Start an interactive window resize.
 *----------------------------------------------------------------------
 */

void
TkWaylandWmResize(TkWaylandWmWindow *win, struct wl_seat *seat,
                  uint32_t serial, TkWaylandResizeEdge edge)
{
    enum xdg_toplevel_resize_edge xdg_edge;
    
    if (!win || !win->xdg_toplevel || !seat) {
        return;
    }
    
    xdg_edge = TkWaylandResizeEdgeToXdg(edge);
    xdg_toplevel_resize(win->xdg_toplevel, seat, serial, xdg_edge);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandWmMaximize --
 *
 *      Maximize the window.
 *----------------------------------------------------------------------
 */

void
TkWaylandWmMaximize(TkWaylandWmWindow *win)
{
    if (!win || !win->xdg_toplevel) {
        return;
    }
    
    xdg_toplevel_set_maximized(win->xdg_toplevel);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandWmUnmaximize --
 *
 *      Unmaximize the window.
 *----------------------------------------------------------------------
 */

void
TkWaylandWmUnmaximize(TkWaylandWmWindow *win)
{
    if (!win || !win->xdg_toplevel) {
        return;
    }
    
    xdg_toplevel_unset_maximized(win->xdg_toplevel);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandWmMinimize --
 *
 *      Minimize (iconify) the window.
 *----------------------------------------------------------------------
 */

void
TkWaylandWmMinimize(TkWaylandWmWindow *win)
{
    if (!win || !win->xdg_toplevel) {
        return;
    }
    
    xdg_toplevel_set_minimized(win->xdg_toplevel);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandWmFullscreen --
 *
 *      Set the window to fullscreen.
 *----------------------------------------------------------------------
 */

void
TkWaylandWmFullscreen(TkWaylandWmWindow *win, struct wl_output *output)
{
    if (!win || !win->xdg_toplevel) {
        return;
    }
    
    xdg_toplevel_set_fullscreen(win->xdg_toplevel, output);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandWmUnfullscreen --
 *
 *      Unset fullscreen.
 *----------------------------------------------------------------------
 */

void
TkWaylandWmUnfullscreen(TkWaylandWmWindow *win)
{
    if (!win || !win->xdg_toplevel) {
        return;
    }
    
    xdg_toplevel_unset_fullscreen(win->xdg_toplevel);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandWmClose --
 *
 *      Request the window to close.
 *----------------------------------------------------------------------
 */

void
TkWaylandWmClose(TkWaylandWmWindow *win)
{
    if (!win || !win->xdg_toplevel) {
        return;
    }
    
    /* No direct close in protocol - rely on close callback from compositor. */
    xdg_toplevel_destroy(win->xdg_toplevel);
    win->xdg_toplevel = NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandWmSetMinSize --
 *
 *      Set minimum window size.
 *----------------------------------------------------------------------
 */

void
TkWaylandWmSetMinSize(TkWaylandWmWindow *win, int min_width, int min_height)
{
    if (!win || !win->xdg_toplevel) {
        return;
    }
    
    xdg_toplevel_set_min_size(win->xdg_toplevel, min_width, min_height);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandWmSetMaxSize --
 *
 *      Set maximum window size.
 *----------------------------------------------------------------------
 */

void
TkWaylandWmSetMaxSize(TkWaylandWmWindow *win, int max_width, int max_height)
{
    if (!win || !win->xdg_toplevel) {
        return;
    }
    
    xdg_toplevel_set_max_size(win->xdg_toplevel, max_width, max_height);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandWmSetWindowGeometry --
 *
 *      Set the window geometry (excluding decorations).
 *----------------------------------------------------------------------
 */

void
TkWaylandWmSetWindowGeometry(TkWaylandWmWindow *win, int x, int y,
                             int width, int height)
{
    if (!win || !win->xdg_surface) {
        return;
    }
    
    xdg_surface_set_window_geometry(win->xdg_surface, x, y, width, height);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandWmCommit --
 *
 *      Commit the surface state.
 *----------------------------------------------------------------------
 */

void
TkWaylandWmCommit(TkWaylandWmWindow *win)
{
    if (!win || !win->surface) {
        return;
    }
    
    wl_surface_commit(win->surface);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandWmMap --
 *
 *      Map the window (make it visible).
 *----------------------------------------------------------------------
 */

void
TkWaylandWmMap(TkWaylandWmWindow *win)
{
    if (!win || !win->surface) {
        return;
    }
    
    wl_surface_commit(win->surface);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandWmIsMaximized --
 *
 *      Return whether the window is maximized.
 *----------------------------------------------------------------------
 */

int
TkWaylandWmIsMaximized(TkWaylandWmWindow *win)
{
    return win ? win->maximized : 0;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandWmIsFullscreen --
 *
 *      Return whether the window is fullscreen.
 *----------------------------------------------------------------------
 */

int
TkWaylandWmIsFullscreen(TkWaylandWmWindow *win)
{
    return win ? win->fullscreen : 0;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandWmGetTitle --
 *
 *      Return the window title.
 *----------------------------------------------------------------------
 */

const char *
TkWaylandWmGetTitle(TkWaylandWmWindow *win)
{
    return win ? win->title : NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandDetectServerDecorations --
 *
 *      Detect whether the Wayland compositor supports server‑side
 *      decorations (SSD).  The result is cached in the static variable
 *      'ssdAvailable'.
 *
 * Results:
 *      1 if SSD is available, 0 otherwise.
 *
 * Side effects:
 *      Sets the global 'ssdAvailable' flag.
 *
 *----------------------------------------------------------------------
 */
 
static int
TkWaylandDetectServerDecorations(void)
{
    const char *desktop = getenv("XDG_CURRENT_DESKTOP");
    const char *session = getenv("XDG_SESSION_TYPE");

    if (session == NULL || strcmp(session, "wayland") != 0) {
	ssdAvailable = 0;
	return 0;
    }

    if (desktop != NULL) {
	if (strstr(desktop, "GNOME") != NULL) {
	    ssdAvailable = 0;
	    return 0;
	}
	if (strstr(desktop, "KDE") != NULL) {
	    ssdAvailable = 1;
	    return 1;
	}
	if (strstr(desktop, "sway") != NULL || strstr(desktop, "Sway") != NULL) {
	    ssdAvailable = 1;
	    return 1;
	}
    }

    ssdAvailable = 0;
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandSetDecorationMode --
 *
 *      Set the global decoration policy from a string.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Changes the 'decorationMode' static variable.
 *
 *----------------------------------------------------------------------
 */
void
TkWaylandSetDecorationMode(const char *mode)
{
    if (mode == NULL) {
	decorationMode = DECOR_AUTO;
	return;
    }

    if (strcmp(mode, "auto") == 0) {
	decorationMode = DECOR_AUTO;
    } else if (strcmp(mode, "server") == 0 || strcmp(mode, "ssd") == 0) {
	decorationMode = DECOR_SERVER_ONLY;
    } else if (strcmp(mode, "client") == 0 || strcmp(mode, "csd") == 0) {
	decorationMode = DECOR_CLIENT_ONLY;
    } else if (strcmp(mode, "none") == 0 || strcmp(mode, "borderless") == 0) {
	decorationMode = DECOR_NONE;
    } else {
	decorationMode = DECOR_AUTO;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandGetDecorationMode --
 *
 *      Return the current decoration mode as a string.
 *
 * Results:
 *      Constant string describing the mode ("auto", "server", "client", "none").
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
const char *
TkWaylandGetDecorationMode(void)
{
    switch (decorationMode) {
    case DECOR_AUTO:        return "auto";
    case DECOR_SERVER_ONLY: return "server";
    case DECOR_CLIENT_ONLY: return "client";
    case DECOR_NONE:        return "none";
    default:                return "auto";
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandShouldUseCSD --
 *
 *      Determine, based on the current policy and detected SSD
 *      availability, whether client‑side decorations should be used.
 *
 * Results:
 *      1 if CSD should be used, 0 otherwise.
 *
 * Side effects:
 *      May trigger the one‑time SSD detection.
 *
 *----------------------------------------------------------------------
 */
 
int
TkWaylandShouldUseCSD(void)
{
    static int detected = 0;

    if (!detected) {
	TkWaylandDetectServerDecorations();
	detected = 1;
    }

    switch (decorationMode) {
    case DECOR_AUTO:
	return 1;
    case DECOR_SERVER_ONLY:
	return 0;
    case DECOR_CLIENT_ONLY:
	return 1;
    case DECOR_NONE:
	return 0;
    default:
	return 1;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandConfigureWindowDecorations --
 *
 *      Set GLFW window hints according to the current decoration policy.
 *      This function should be called before creating a window.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Modifies the GLFW hint state.
 *
 *----------------------------------------------------------------------
 */
 
void
TkWaylandConfigureWindowDecorations(void)
{
    int useCSD = TkWaylandShouldUseCSD();

    if (decorationMode == DECOR_NONE) {
	glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    } else if (useCSD) {
	glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    } else {
	glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandConfigureCallback --
 *
 *      Callback for Wayland window configure events.
 *----------------------------------------------------------------------
 */

static void
TkWaylandConfigureCallback(void *data, int width, int height)
{
    TkWaylandDecoration *decor = (TkWaylandDecoration *)data;
    
    if (!decor || !decor->winPtr) {
        return;
    }
    
    /* Update window size in mapping */
    if (decor->glfwWindow) {
        TkGlfwUpdateWindowSize(decor->glfwWindow, width, height);
    }
    
    /* Queue expose event for redraw */
    TkWaylandQueueExposeEvent(decor->winPtr, 0, 0, width, height);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandCloseCallback --
 *
 *      Callback for Wayland window close events.
 *----------------------------------------------------------------------
 */

static void
TkWaylandCloseCallback(void *data)
{
    TkWaylandDecoration *decor = (TkWaylandDecoration *)data;
    
    if (!decor || !decor->glfwWindow) {
        return;
    }
    
    glfwSetWindowShouldClose(decor->glfwWindow, GLFW_TRUE);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandCreateDecoration --
 *
 *      Allocate and initialise a decoration structure for a Tk window.
 *      The wmPtr is taken from winPtr->wmInfoPtr (must be valid).
 *
 * Results:
 *      Pointer to a new TkWaylandDecoration, or NULL on failure.
 *
 * Side effects:
 *      Memory is allocated; the window title is set from the Tk path name.
 *
 *----------------------------------------------------------------------
 */
 
TkWaylandDecoration *
TkWaylandCreateDecoration(TkWindow *winPtr,
			  GLFWwindow *glfwWindow)
{
    TkWaylandDecoration *decor;
    TkWaylandPlatformInfo *platformInfo;

    if (winPtr == NULL || glfwWindow == NULL) {
	return NULL;
    }

    decor = (TkWaylandDecoration *)calloc(1, sizeof(TkWaylandDecoration));
    if (decor == NULL) {
	return NULL;
    }

    decor->winPtr = winPtr;
    decor->glfwWindow = glfwWindow;
    decor->wmPtr = (WmInfo *)winPtr->wmInfoPtr;  /* Link to WM info. */
    decor->enabled = 1;
    decor->maximized = 0;

    decor->title = (char *)malloc(256);
    if (decor->title != NULL) {
	const char *name = Tk_PathName((Tk_Window)winPtr);
	strncpy(decor->title, name ? name : "Tk", 255);
	decor->title[255] = '\0';
    }

    decor->closeState = BUTTON_NORMAL;
    decor->maxState = BUTTON_NORMAL;
    decor->minState = BUTTON_NORMAL;
    decor->dragging = 0;
    decor->resizing = RESIZE_NONE;

    /* Create Wayland window management object. */
    platformInfo = TkGetWaylandPlatformInfo(); 
    if (platformInfo && platformInfo->wm_context) {
        /* Get surface from mapping */
        WindowMapping *mapping = FindMappingByGLFW(glfwWindow);
        if (mapping && mapping->surface) {
            decor->wm_win = TkWaylandWmCreateWindow(
                platformInfo->wm_context,
                mapping->surface,
                TkWaylandConfigureCallback,
                TkWaylandCloseCallback,
                decor);
        }
    }

    return decor;
}
/*
 *----------------------------------------------------------------------
 *
 * TkWaylandDestroyDecoration --
 *
 *      Free the resources associated with a decoration structure.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Memory is freed.
 *
 *----------------------------------------------------------------------
 */
 
void
TkWaylandDestroyDecoration(TkWaylandDecoration *decor)
{
    if (decor == NULL) {
	return;
    }

    if (decor->title != NULL) {
	free(decor->title);
    }
    
    if (decor->wm_win != NULL) {
        TkWaylandWmDestroyWindow(decor->wm_win);
    }

    free(decor);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandDrawDecoration --
 *
 *      Draw the complete window decoration (shadow, border, title bar)
 *      using the NanoVG context.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Issues NanoVG drawing commands.
 *
 *----------------------------------------------------------------------
 */
 
void
TkWaylandDrawDecoration(TkWaylandDecoration *decor,
                        NVGcontext *vg)
{
    int width, height;
    WindowMapping *mapping;

    if (decor == NULL || !decor->enabled || vg == NULL) {
        return;
    }

    glfwGetWindowSize(decor->glfwWindow, &width, &height);

    /* Get the client area size from mapping */
    mapping = FindMappingByGLFW(decor->glfwWindow);
    if (!mapping) return;

    nvgSave(vg);

    /* Draw shadow (outside window bounds). */
    NVGpaint shadowPaint = nvgBoxGradient(vg,
                                          -BORDER_WIDTH, -TITLE_BAR_HEIGHT,
                                          width + 2*BORDER_WIDTH,
                                          height + TITLE_BAR_HEIGHT + BORDER_WIDTH,
                                          CORNER_RADIUS, SHADOW_BLUR,
                                          nvgRGBA(0, 0, 0, 64), nvgRGBA(0, 0, 0, 0));
    nvgBeginPath(vg);
    nvgRect(vg, -SHADOW_BLUR - BORDER_WIDTH, -SHADOW_BLUR - TITLE_BAR_HEIGHT,
            width + 2*(SHADOW_BLUR + BORDER_WIDTH),
            height + 2*SHADOW_BLUR + TITLE_BAR_HEIGHT + BORDER_WIDTH);
    nvgFillPaint(vg, shadowPaint);
    nvgFill(vg);

    /* Draw border and title bar. */
    DrawBorder(vg, decor, width, height);
    DrawTitleBar(vg, decor, width, height);

    /* Set scissor to client area for widget drawing */
    nvgIntersectScissor(vg, BORDER_WIDTH, TITLE_BAR_HEIGHT,
                        mapping->width, mapping->height);

    nvgRestore(vg);
}


/*
 *----------------------------------------------------------------------
 *
 * DrawTitleBar --
 *
 *      Draw the title bar background, title text, and window control
 *      buttons.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Issues NanoVG drawing commands.
 *
 *----------------------------------------------------------------------
 */
static void
DrawTitleBar(NVGcontext *vg,
	     TkWaylandDecoration *decor,
	     int width,
	     TCL_UNUSED(int)) /* height */

{
    float buttonX;
    int focused;
    NVGcolor bgColor, textColor;

    focused = glfwGetWindowAttrib(decor->glfwWindow, GLFW_FOCUSED);
    bgColor = focused ? nvgRGB(45, 45, 48) : nvgRGB(60, 60, 60);

    nvgBeginPath(vg);
    nvgRoundedRectVarying(vg, 0, 0, width, TITLE_BAR_HEIGHT,
			  CORNER_RADIUS, CORNER_RADIUS, 0, 0);
    nvgFillColor(vg, bgColor);
    nvgFill(vg);

    if (decor->title != NULL) {
	textColor = focused ? nvgRGB(255, 255, 255) : nvgRGB(180, 180, 180);
	nvgFontSize(vg, 14.0f);
	nvgFontFaceId(vg, TkGlfwGetContext()->decorFontId);
	nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
	nvgFillColor(vg, textColor);
	nvgText(vg, 15, TITLE_BAR_HEIGHT / 2, decor->title, NULL);
    }

    buttonX = width - BUTTON_SPACING - BUTTON_WIDTH;
    DrawButton(vg, BUTTON_CLOSE, decor->closeState,
	       buttonX, (TITLE_BAR_HEIGHT - BUTTON_HEIGHT) / 2,
	       BUTTON_WIDTH, BUTTON_HEIGHT);

    buttonX -= (BUTTON_WIDTH + BUTTON_SPACING);
    DrawButton(vg, BUTTON_MAXIMIZE, decor->maxState,
	       buttonX, (TITLE_BAR_HEIGHT - BUTTON_HEIGHT) / 2,
	       BUTTON_WIDTH, BUTTON_HEIGHT);

    buttonX -= (BUTTON_WIDTH + BUTTON_SPACING);
    DrawButton(vg, BUTTON_MINIMIZE, decor->minState,
	       buttonX, (TITLE_BAR_HEIGHT - BUTTON_HEIGHT) / 2,
	       BUTTON_WIDTH, BUTTON_HEIGHT);
}

/*
 *----------------------------------------------------------------------
 *
 * DrawBorder --
 *
 *      Draw the outer border of the window.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Issues NanoVG drawing commands.
 *
 *----------------------------------------------------------------------
 */
 
static void
DrawBorder(NVGcontext *vg,
	   TkWaylandDecoration *decor,
	   int width,
	   int height)
{
    int focused;
    NVGcolor borderColor;

    focused = glfwGetWindowAttrib(decor->glfwWindow, GLFW_FOCUSED);
    borderColor = focused ? nvgRGB(30, 30, 30) : nvgRGB(80, 80, 80);

    nvgBeginPath(vg);
    nvgRoundedRect(vg, 0, 0, width, height, CORNER_RADIUS);
    nvgStrokeColor(vg, borderColor);
    nvgStrokeWidth(vg, BORDER_WIDTH);
    nvgStroke(vg);
}

/*
 *----------------------------------------------------------------------
 *
 * DrawButton --
 *
 *      Draw one window control button (close, maximise, minimise) with
 *      appropriate background and icon based on its state.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Issues NanoVG drawing commands.
 *
 *----------------------------------------------------------------------
 */
 
static void
DrawButton(NVGcontext *vg,
	   ButtonType type,
	   ButtonState state,
	   float x,
	   float y,
	   float w,
	   float h)
{
    NVGcolor bgColor, iconColor;
    float iconSize = 10.0f;
    float cx = x + w/2;
    float cy = y + h/2;

    switch (state) {
    case BUTTON_HOVER:
	bgColor = (type == BUTTON_CLOSE) ? nvgRGB(232, 17, 35) : nvgRGB(80, 80, 80);
	break;
    case BUTTON_PRESSED:
	bgColor = (type == BUTTON_CLOSE) ? nvgRGB(196, 43, 28) : nvgRGB(100, 100, 100);
	break;
    case BUTTON_NORMAL:
    default:
	bgColor = nvgRGBA(0, 0, 0, 0);
	break;
    }

    if (state != BUTTON_NORMAL) {
	nvgBeginPath(vg);
	nvgRoundedRect(vg, x, y, w, h, 3.0f);
	nvgFillColor(vg, bgColor);
	nvgFill(vg);
    }

    iconColor = (state == BUTTON_HOVER || state == BUTTON_PRESSED) ?
	nvgRGB(255, 255, 255) : nvgRGB(200, 200, 200);

    nvgStrokeColor(vg, iconColor);
    nvgStrokeWidth(vg, 1.5f);

    switch (type) {
    case BUTTON_CLOSE:
	nvgBeginPath(vg);
	nvgMoveTo(vg, cx - iconSize/2, cy - iconSize/2);
	nvgLineTo(vg, cx + iconSize/2, cy + iconSize/2);
	nvgMoveTo(vg, cx + iconSize/2, cy - iconSize/2);
	nvgLineTo(vg, cx - iconSize/2, cy + iconSize/2);
	nvgStroke(vg);
	break;
    case BUTTON_MAXIMIZE:
	nvgBeginPath(vg);
	nvgRect(vg, cx - iconSize/2, cy - iconSize/2, iconSize, iconSize);
	nvgStroke(vg);
	break;
    case BUTTON_MINIMIZE:
	nvgBeginPath(vg);
	nvgMoveTo(vg, cx - iconSize/2, cy);
	nvgLineTo(vg, cx + iconSize/2, cy);
	nvgStroke(vg);
	break;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandDecorationMouseButton --
 *
 *       Process mouse button events for the decoration area.
 *  
 *       On press:
 *       Button hits are recorded (PRESSED state). Title-bar press delegates the 
 *       drag to the compositor via TkWaylandWmMove(); no local drag state is 
 *       maintained. Border-edge press delegates resize to the compositor via 
 *       TkWaylandWmResize(); no local resize state is maintained.
 * 
 *       On release:
 *       A button is activated if it was in PRESSED state and the cursor is still 
 *       over it. All button states are reset and hover is recomputed.
 *
 * Results:
 *       1 if the event was handled (i.e. occurred in the decoration area),
 *       0 otherwise.
 *
 * Side effects:
 *      May initiate window dragging, resizing, or button state changes.
 *      May trigger window close, maximise, or minimize actions.
 *----------------------------------------------------------------------
 */
 
int
TkWaylandDecorationMouseButton(TkWaylandDecoration *decor,
			       int button,
			       int action,
			       double x,
			       double y)
{
    int width, height;
    int buttonType;
    int resizeEdge;
    TkWaylandWmWindow *wm_win;
    struct wl_seat *seat;
    uint32_t serial;
    TkWaylandPlatformInfo *platformInfo;


    if (decor == NULL || !decor->enabled) {
	return 0;
    }

    glfwGetWindowSize(decor->glfwWindow, &width, &height);

    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        /* Get seat from global platform info */
        platformInfo = TkGetWaylandPlatformInfo();
        if (platformInfo) {
            seat = platformInfo->seat;
            serial = platformInfo->last_serial;
        } else {
            seat = NULL;
            serial = 0;
        }

	if (action == GLFW_PRESS) {

	    /* Check window control buttons first. */
	    buttonType = GetButtonAtPosition(decor, x, y, width);
	    if (buttonType >= 0) {
		if (buttonType == BUTTON_CLOSE)    decor->closeState = BUTTON_PRESSED;
		else if (buttonType == BUTTON_MAXIMIZE) decor->maxState = BUTTON_PRESSED;
		else if (buttonType == BUTTON_MINIMIZE) decor->minState = BUTTON_PRESSED;
		return 1;
	    }

	    /* Title bar drag — hand off to compositor via Wayland. */
	    if (y < TITLE_BAR_HEIGHT) {
                wm_win = decor->wm_win;
                if (wm_win && seat) {
                    TkWaylandWmMove(wm_win, seat, serial);
                }
		return 1;
	    }

	    /* Border resize — hand off to compositor via Wayland. */
	    resizeEdge = GetResizeEdge(x, y, width, height);
	    if (resizeEdge != RESIZE_NONE) {
                wm_win = decor->wm_win;
                if (wm_win && seat) {
                    TkWaylandWmResize(wm_win, seat, serial, 
                                      TkWaylandResizeEdgeFromInt(resizeEdge));
                }
		return 1;
	    }

	} else if (action == GLFW_RELEASE) {

	    /* Activate a button only if it was pressed and cursor is still on it. */
	    buttonType = GetButtonAtPosition(decor, x, y, width);
	    if (buttonType >= 0) {
		if (buttonType == BUTTON_CLOSE    && decor->closeState == BUTTON_PRESSED)
		    HandleButtonClick(decor, BUTTON_CLOSE);
		else if (buttonType == BUTTON_MAXIMIZE && decor->maxState  == BUTTON_PRESSED)
		    HandleButtonClick(decor, BUTTON_MAXIMIZE);
		else if (buttonType == BUTTON_MINIMIZE && decor->minState  == BUTTON_PRESSED)
		    HandleButtonClick(decor, BUTTON_MINIMIZE);
	    }

	    /* Reset all button states and recompute hover. */
	    decor->closeState = BUTTON_NORMAL;
	    decor->maxState   = BUTTON_NORMAL;
	    decor->minState   = BUTTON_NORMAL;
	    UpdateButtonStates(decor, x, y, width);
	    return 1;
	}
    }

    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandDecorationMouseMove --
 *
 *      Process mouse motion events for the decoration area. Drag and resize are 
 *      compositor-managed (initiated in TkWaylandDecorationMouseButton) so this function 
 *      only needs to keep the button hover states current.
 *
 * Results:
 *       Always returns 0 — motion in the decoration area is not
 *       consumed; Tk still receives MotionNotify for cursor updates.
 *
 * Side effects:
 *      Updates button hover states in the decoration structure.
 *
 *----------------------------------------------------------------------
 */
 
int
TkWaylandDecorationMouseMove(TkWaylandDecoration *decor,
			     double x,
			     double y)
{
    int width, height;


    if (decor == NULL || !decor->enabled) {
	return 0;
    }

    glfwGetWindowSize(decor->glfwWindow, &width, &height);
    UpdateButtonStates(decor, x, y, width);

    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * HandleButtonClick --
 *
 *      Perform the action associated with a window control button.
 *      For maximize, update the WM's zoomed attribute to stay in sync.
 *      For minimize, update the WM's iconic state.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      May close, maximise/restore, or minimise the window via Wayland.
 *      Updates the WmInfo zoomed/iconic flags.
 *
 *----------------------------------------------------------------------
 */
 
static void
HandleButtonClick(TkWaylandDecoration *decor,
		  ButtonType button)
{
    switch (button) {
    case BUTTON_CLOSE:
        if (decor->wm_win) {
            TkWaylandWmClose(decor->wm_win);
        }
        glfwSetWindowShouldClose(decor->glfwWindow, GLFW_TRUE);
	break;
    case BUTTON_MAXIMIZE:
	if (decor->maximized) {
            if (decor->wm_win) {
                TkWaylandWmUnmaximize(decor->wm_win);
            }
	    decor->maximized = 0;
	    /* Update WM's zoomed attribute. */
	    if (decor->wmPtr != NULL) {
		decor->wmPtr->attributes.zoomed = 0;
		decor->wmPtr->reqState.zoomed = 0;
	    }
	} else {
            if (decor->wm_win) {
                TkWaylandWmMaximize(decor->wm_win);
            }
	    decor->maximized = 1;
	    if (decor->wmPtr != NULL) {
		decor->wmPtr->attributes.zoomed = 1;
		decor->wmPtr->reqState.zoomed = 1;
	    }
	}
	break;
    case BUTTON_MINIMIZE:
        if (decor->wm_win) {
            TkWaylandWmMinimize(decor->wm_win);
        }
	/* Update Tk's internal state to IconicState. */
	if (decor->winPtr != NULL) {
	    TkpWmSetState(decor->winPtr, IconicState);
	    /* GLFW may not send an UnmapNotify, so clear mapped flag manually. */
	    decor->winPtr->flags &= ~TK_MAPPED;
	}
	break;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * GetButtonAtPosition --
 *
 *      Determine which window control button, if any, is under the given
 *      coordinates.
 *
 * Results:
 *      The ButtonType of the button, or -1 if none.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
 
static int
GetButtonAtPosition(TkWaylandDecoration *decor,
		    double x,
		    double y,
		    int width)
{
    float buttonX;

    (void)decor;

    if (y < 0 || y > TITLE_BAR_HEIGHT) {
	return -1;
    }

    buttonX = width - BUTTON_SPACING - BUTTON_WIDTH;
    if (x >= buttonX && x < buttonX + BUTTON_WIDTH) return BUTTON_CLOSE;

    buttonX -= (BUTTON_WIDTH + BUTTON_SPACING);
    if (x >= buttonX && x < buttonX + BUTTON_WIDTH) return BUTTON_MAXIMIZE;

    buttonX -= (BUTTON_WIDTH + BUTTON_SPACING);
    if (x >= buttonX && x < buttonX + BUTTON_WIDTH) return BUTTON_MINIMIZE;

    return -1;
}

/*
 *----------------------------------------------------------------------
 *
 * GetResizeEdge --
 *
 *      Determine which edges (if any) are being approached for resizing,
 *      based on the cursor position relative to the window borders.
 *
 * Results:
 *      Bitmask of RESIZE_* flags.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
 
static int
GetResizeEdge(double x,
	      double y,
	      int width,
	      int height)
{
    int edge = RESIZE_NONE;
    int margin = 5;

    if (x < margin) edge |= RESIZE_LEFT;
    else if (x > width - margin) edge |= RESIZE_RIGHT;

    if (y < margin) edge |= RESIZE_TOP;
    else if (y > height - margin) edge |= RESIZE_BOTTOM;

    return edge;
}

/*
 *----------------------------------------------------------------------
 *
 * UpdateButtonStates --
 *
 *      Update the hover state of the three window buttons based on the
 *      current cursor position.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Modifies the button state fields in the decoration structure.
 *
 *----------------------------------------------------------------------
 */
 
static void
UpdateButtonStates(TkWaylandDecoration *decor,
		   double x,
		   double y,
		   int width)
{
    int button = GetButtonAtPosition(decor, x, y, width);

    decor->closeState = BUTTON_NORMAL;
    decor->maxState = BUTTON_NORMAL;
    decor->minState = BUTTON_NORMAL;

    if (button == BUTTON_CLOSE) decor->closeState = BUTTON_HOVER;
    else if (button == BUTTON_MAXIMIZE) decor->maxState = BUTTON_HOVER;
    else if (button == BUTTON_MINIMIZE) decor->minState = BUTTON_HOVER;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandSetDecorationTitle --
 *
 *      Change the title displayed in the window decoration.
 *      This function should be called by the window manager (tkWaylandWm.c)
 *      whenever the window title changes (e.g., via "wm title").
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The title string is duplicated and stored; next redraw uses it.
 *
 *----------------------------------------------------------------------
 */
void
TkWaylandSetDecorationTitle(TkWaylandDecoration *decor,
			    const char *title)
{
    if (decor == NULL || title == NULL) {
	return;
    }

    if (decor->title != NULL) {
	free(decor->title);
    }

    decor->title = (char *)malloc(strlen(title) + 1);
    if (decor->title != NULL) {
	strcpy(decor->title, title);
    }
    
    /* Update Wayland window title */
    if (decor->wm_win != NULL) {
        TkWaylandWmSetTitle(decor->wm_win, title);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandSetWindowMaximized --
 *
 *      Update the decoration's internal maximized state to match the
 *      WM's zoomed attribute.  Called by the WM when the window is
 *      maximized or restored programmatically.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Updates decor->maximized; next redraw will show correct button.
 *
 *----------------------------------------------------------------------
 */
void
TkWaylandSetWindowMaximized(TkWaylandDecoration *decor,
			    int maximized)
{
    if (decor == NULL) {
	return;
    }
    decor->maximized = maximized ? 1 : 0;
    
    /* Update Wayland window state if needed */
    if (decor->wm_win != NULL) {
        if (maximized && !TkWaylandWmIsMaximized(decor->wm_win)) {
            TkWaylandWmMaximize(decor->wm_win);
        } else if (!maximized && TkWaylandWmIsMaximized(decor->wm_win)) {
            TkWaylandWmUnmaximize(decor->wm_win);
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandGetDecorationContentArea --
 *
 *      Return the rectangle (relative to the window) that is available
 *      for application content, i.e. excluding the decoration areas.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The output parameters are filled with the content area geometry.
 *
 *----------------------------------------------------------------------
 */
 
void
TkWaylandGetDecorationContentArea(TkWaylandDecoration *decor,
				  int *x,
				  int *y,
				  int *width,
				  int *height)
{
    int winWidth, winHeight;

    if (decor == NULL) {
	return;
    }

    glfwGetWindowSize(decor->glfwWindow, &winWidth, &winHeight);

    if (decor->enabled) {
	*x = BORDER_WIDTH;
	*y = TITLE_BAR_HEIGHT;
	*width = winWidth - 2 * BORDER_WIDTH;
	*height = winHeight - TITLE_BAR_HEIGHT - BORDER_WIDTH;
    } else {
	*x = 0;
	*y = 0;
	*width = winWidth;
	*height = winHeight;
    }
}


/*
 *----------------------------------------------------------------------
 * TkWaylandInitDecorationPolicy --
 *
 *	Initialize the Wayland decoration system. Detects compositor
 *	capabilities, and sets policy from environment.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Detects SSD availability, and sets decoration mode.
 *  
 *----------------------------------------------------------------------
 */

void
TkWaylandInitDecorationPolicy(TCL_UNUSED(Tcl_Interp *))
{
    const char *decorEnv;
    
    /* Detect whether compositor supports server-side decorations. */
    TkWaylandDetectServerDecorations();
    
    /* Check for environment variable override. */
    decorEnv = getenv("TK_WAYLAND_DECORATIONS");
    if (decorEnv != NULL) {
        TkWaylandSetDecorationMode(decorEnv);
    }
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 79
 * coding: utf-8
 * End:
 */
