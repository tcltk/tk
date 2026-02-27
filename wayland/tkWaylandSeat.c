/*
 * tkWaylandSeat.c --
 *
 *      Wayland wl_seat listener: binds the seat from the registry,
 *      gets the wl_pointer, and installs a lightweight pointer listener
 *      whose only job is to keep platformInfo->last_serial current so
 *      that xdg_toplevel_move and xdg_toplevel_resize receive a valid
 *      serial and are not silently rejected by the compositor.
 *
 *      Call TkWaylandSeatInit() once during platform initialisation,
 *      after the wl_display and registry are available (i.e. after
 *      wl_display_roundtrip has returned globals).
 *
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkGlfwInt.h"
#include <wayland-client.h>
#include <string.h>

/* Forward declarations. */
static void seat_capabilities_handler(void *data, struct wl_seat *seat,
                                       uint32_t capabilities);
static void seat_name_handler(void *data, struct wl_seat *seat,
                               const char *name);
static void pointer_enter_handler(void *data, struct wl_pointer *pointer,
                                   uint32_t serial, struct wl_surface *surface,
                                   wl_fixed_t sx, wl_fixed_t sy);
static void pointer_leave_handler(void *data, struct wl_pointer *pointer,
                                   uint32_t serial, struct wl_surface *surface);
static void pointer_motion_handler(void *data, struct wl_pointer *pointer,
                                    uint32_t time, wl_fixed_t sx, wl_fixed_t sy);
static void pointer_button_handler(void *data, struct wl_pointer *pointer,
                                    uint32_t serial, uint32_t time,
                                    uint32_t button, uint32_t state);
static void pointer_axis_handler(void *data, struct wl_pointer *pointer,
                                  uint32_t time, uint32_t axis,
                                  wl_fixed_t value);

/* ------------------------------------------------------------------ */
/* Internal state.                                                       */
/* ------------------------------------------------------------------ */

static struct wl_seat    *tk_seat    = NULL;
static struct wl_pointer *tk_pointer = NULL;

/* ------------------------------------------------------------------ */
/* Pointer listener — only pointer_button_handler and                  */
/* pointer_enter_handler do real work; the rest are mandatory stubs.   */
/* ------------------------------------------------------------------ */

static void
pointer_enter_handler(void *data,
                      TCL_UNUSED(struct wl_pointer *),
                      uint32_t serial,
                      TCL_UNUSED(struct wl_surface *),
                      TCL_UNUSED(wl_fixed_t),
                      TCL_UNUSED(wl_fixed_t))
{
    TkWaylandPlatformInfo *info = (TkWaylandPlatformInfo *)data;
    if (info) {
        info->last_serial = serial;
    }
}

static void
pointer_leave_handler(TCL_UNUSED(void *),
                      TCL_UNUSED(struct wl_pointer *),
                      TCL_UNUSED(uint32_t),
                      TCL_UNUSED(struct wl_surface *))
{
    /* Nothing needed. */
}

static void
pointer_motion_handler(TCL_UNUSED(void *),
                       TCL_UNUSED(struct wl_pointer *),
                       TCL_UNUSED(uint32_t),
                       TCL_UNUSED(wl_fixed_t),
                       TCL_UNUSED(wl_fixed_t))
{
    /* Nothing needed — GLFW handles motion events. */
}

static void
pointer_button_handler(void *data,
                       TCL_UNUSED(struct wl_pointer *),
                       uint32_t serial,
                       TCL_UNUSED(uint32_t),   /* time */
                       TCL_UNUSED(uint32_t),   /* button */
                       TCL_UNUSED(uint32_t))   /* state */
{
    /*
     * This fires before GLFW's own pointer listener processes the same
     * event, so by the time TkGlfwMouseButtonCallback runs and calls
     * TkWaylandDecorationMouseButton, last_serial already holds the
     * correct value for xdg_toplevel_move / xdg_toplevel_resize.
     */
    TkWaylandPlatformInfo *info = (TkWaylandPlatformInfo *)data;
    if (info) {
        info->last_serial = serial;
    }
}

static void
pointer_axis_handler(TCL_UNUSED(void *),
                     TCL_UNUSED(struct wl_pointer *),
                     TCL_UNUSED(uint32_t),
                     TCL_UNUSED(uint32_t),
                     TCL_UNUSED(wl_fixed_t))
{
    /* Nothing needed — GLFW handles scroll events. */
}

static const struct wl_pointer_listener tk_pointer_listener = {
    .enter  = pointer_enter_handler,
    .leave  = pointer_leave_handler,
    .motion = pointer_motion_handler,
    .button = pointer_button_handler,
    .axis   = pointer_axis_handler,
};

/* ------------------------------------------------------------------ */
/* Seat listener                                                        */
/* ------------------------------------------------------------------ */

static void
seat_capabilities_handler(void *data,
                           struct wl_seat *seat,
                           uint32_t capabilities)
{
    TkWaylandPlatformInfo *info = (TkWaylandPlatformInfo *)data;

    if (capabilities & WL_SEAT_CAPABILITY_POINTER) {
        if (!tk_pointer) {
            tk_pointer = wl_seat_get_pointer(seat);
            if (tk_pointer) {
                wl_pointer_add_listener(tk_pointer,
                                        &tk_pointer_listener, info);
            }
        }
    } else {
        /* Pointer capability removed — clean up. */
        if (tk_pointer) {
            wl_pointer_destroy(tk_pointer);
            tk_pointer = NULL;
        }
    }
}

static void
seat_name_handler(TCL_UNUSED(void *),
                  TCL_UNUSED(struct wl_seat *),
                  TCL_UNUSED(const char *))
{
    /* Nothing needed. */
}

static const struct wl_seat_listener tk_seat_listener = {
    .capabilities = seat_capabilities_handler,
    .name         = seat_name_handler,
};

/* ------------------------------------------------------------------ */
/* Registry listener — picks up wl_seat from compositor globals        */
/* ------------------------------------------------------------------ */

static void
seat_registry_global_handler(void *data,
                              struct wl_registry *registry,
                              uint32_t name,
                              const char *interface,
                              uint32_t version)
{
    TkWaylandPlatformInfo *info = (TkWaylandPlatformInfo *)data;

    if (strcmp(interface, wl_seat_interface.name) == 0) {
        tk_seat = (struct wl_seat *)wl_registry_bind(
            registry, name, &wl_seat_interface,
            version < 4 ? version : 4);
        if (tk_seat) {
            info->seat = tk_seat;
            wl_seat_add_listener(tk_seat, &tk_seat_listener, info);
        }
    }
}

static void
seat_registry_global_remove_handler(TCL_UNUSED(void *),
                                    TCL_UNUSED(struct wl_registry *),
                                    TCL_UNUSED(uint32_t))
{
    /* Nothing needed. */
}

static const struct wl_registry_listener tk_seat_registry_listener = {
    .global        = seat_registry_global_handler,
    .global_remove = seat_registry_global_remove_handler,
};

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandSeatInit --
 *
 *      Bind the wl_seat from the Wayland registry and install the
 *      pointer serial listener.  Must be called once during platform
 *      initialisation, after the wl_display is available.
 *
 *      Internally this does a second registry + roundtrip specifically
 *      for the seat, so it can be called independently of whatever
 *      other registry work the rest of the backend does.
 *
 * Results:
 *      1 on success, 0 on failure.
 *
 *----------------------------------------------------------------------
 */

int
TkWaylandSeatInit(struct wl_display *display)
{
    struct wl_registry    *registry;
    TkWaylandPlatformInfo *info;

    if (!display) {
        return 0;
    }

    info = TkGetWaylandPlatformInfo();
    if (!info) {
        return 0;
    }

    registry = wl_display_get_registry(display);
    if (!registry) {
        return 0;
    }

    wl_registry_add_listener(registry, &tk_seat_registry_listener, info);
    wl_display_roundtrip(display);  /* Picks up wl_seat global. */
    wl_display_roundtrip(display);  /* Picks up seat capabilities. */

    wl_registry_destroy(registry);

    return (tk_seat != NULL) ? 1 : 0;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandSeatCleanup --
 *
 *      Release the seat and pointer objects.  Call during shutdown.
 *
 *----------------------------------------------------------------------
 */

void
TkWaylandSeatCleanup(void)
{
    if (tk_pointer) {
        wl_pointer_destroy(tk_pointer);
        tk_pointer = NULL;
    }
    if (tk_seat) {
        wl_seat_destroy(tk_seat);
        tk_seat = NULL;
    }
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
