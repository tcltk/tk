/*
 * tkWaylandPopup.c --
 *
 *	Native Wayland popup/sub-surface primitive for Tk using wl_shm.
 *	
 *	This module implements a lightweight wrapper around xdg_popup and
 *	wl_subsurface objects using wl_shm for buffer management. It uses
 *	a software renderer that renders to the SHM buffer using NanoVG's
 *	software rendering backend.
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
#include <wayland-util.h>
#include "xdg-shell-client-protocol.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <math.h>

/* NanoVG includes for software rendering */
#define NANOVG_GLES3 1
#include "nanovg.h"
#include "nanovg_gl.h"
#include "nanovg_gl_utils.h"

/* Debug macro. */
#define POPUP_DEBUG 1
#if POPUP_DEBUG
#define POPUP_LOG(fmt, ...) fprintf(stderr, "POPUP_SHM: " fmt "\n", ##__VA_ARGS__)
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
 * Forward declarations from tkWaylandFont.c
 */
MODULE_SCOPE int TkWaylandLoadNamedFontIntoContext(NVGcontext *vg, const char *tkFontName);
MODULE_SCOPE void TkWaylandFontContextDestroyed(NVGcontext *vg);

/*
 * Software renderer context - uses NanoVG with a custom renderer
 * that outputs to a memory buffer.
 */
typedef struct SoftRenderer {
    NVGcontext *vg;           /* NanoVG context for drawing commands */
    unsigned char *pixels;    /* Raw pixel data in RGBA format */
    int width;
    int height;
    int stride;
    int size;
    int needs_redraw;
    GLuint fbo;                /* Offscreen framebuffer sized to width x height */
    GLuint texture;            /* Color attachment backing fbo */
} SoftRenderer;

/*
 * wl_shm buffer structure
 */
typedef struct WlShmBuffer {
    struct wl_buffer *buffer;
    void *data;
    int width;
    int height;
    int stride;
    int size;
    int in_use;
    struct wl_list link;
} WlShmBuffer;

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
    int                    pendingConfigure;
    uint32_t               pendingSerial;
    int                    configureAcked;
    int                    surfaceMapped;
    
    /* Callback. */
    void                  (*doneCallback)(void *clientData);
    void                   *doneClientData;
    
    /* Software renderer */
    SoftRenderer          *renderer;
    
    /* wl_shm buffers */
    struct wl_shm         *shm;
    WlShmBuffer           *current_buffer;
    WlShmBuffer           *pending_buffer;
    struct wl_list        buffers;
    int                    buffer_count;
};

/* Global state for this module. */
static struct {
    struct wl_compositor *compositor;
    struct wl_subcompositor *subcompositor;
    struct xdg_wm_base   *wmBase;
    struct wl_seat       *seat;
    struct wl_shm        *shm;
    struct wl_display    *wlDisplay;
    GLFWwindow           *mainWindow;
    int                   initialized;
    uint32_t              lastSerial;
} popupGlobals = {
    .compositor = NULL,
    .subcompositor = NULL,
    .wmBase = NULL,
    .seat = NULL,
    .shm = NULL,
    .wlDisplay = NULL,
    .mainWindow = NULL,
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
static SoftRenderer* TkWaylandPopupCreateRenderer(int width, int height);
static void TkWaylandPopupDestroyRenderer(SoftRenderer *renderer);
static void TkWaylandPopupRendererClear(SoftRenderer *renderer, 
    unsigned char r, unsigned char g, unsigned char b, unsigned char a);
static WlShmBuffer* TkWaylandPopupCreateShmBuffer(struct wl_shm *shm, int width, int height);
static void TkWaylandPopupDestroyShmBuffer(WlShmBuffer *buffer);
static void TkWaylandPopupReleaseBuffer(void *data, struct wl_buffer *buffer);
static int TkWaylandPopupAttachBuffer(TkWaylandPopup *popup, WlShmBuffer *buffer);
static void TkWaylandPopupCopyPixelsToBuffer(TkWaylandPopup *popup, WlShmBuffer *buffer);

/* wl_buffer listener for release events */
static const struct wl_buffer_listener buffer_listener = {
    .release = TkWaylandPopupReleaseBuffer
};

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
 * TkWaylandPopupCreateRenderer --
 *
 *	Create a software renderer with NanoVG context for drawing.
 *	Loads fonts directly into the context - fonts are context-local.
 *
 * Results:
 *	Pointer to SoftRenderer or NULL on failure.
 *
 * Side effects:
 *	Allocates memory for pixel buffer and creates NanoVG context.
 *----------------------------------------------------------------------
 */

static SoftRenderer*
TkWaylandPopupCreateRenderer(
    int width,
    int height)
{
    SoftRenderer *renderer;
    int stride = width * 4;  /* RGBA */
    int size = stride * height;
    
    POPUP_LOG("TkWaylandPopupCreateRenderer: creating renderer %dx%d", width, height);
    
    renderer = (SoftRenderer *)calloc(1, sizeof(SoftRenderer));
    if (!renderer) {
        POPUP_LOG("TkWaylandPopupCreateRenderer: malloc failed");
        return NULL;
    }
    
    renderer->pixels = (unsigned char *)malloc(size);
    if (!renderer->pixels) {
        POPUP_LOG("TkWaylandPopupCreateRenderer: pixel buffer malloc failed");
        free(renderer);
        return NULL;
    }
    
    renderer->width = width;
    renderer->height = height;
    renderer->stride = stride;
    renderer->size = size;
    renderer->needs_redraw = 1;
    renderer->fbo = 0;
    renderer->texture = 0;
    
    /* Clear the buffer to transparent */
    memset(renderer->pixels, 0, size);
    
    /*
     * We are about to create GL objects (texture, FBO, NanoVG's own
     * GL resources), so make sure a context is current first.
     */
    if (popupGlobals.mainWindow) {
        glfwMakeContextCurrent(popupGlobals.mainWindow);
    } else {
        POPUP_LOG("TkWaylandPopupCreateRenderer: WARNING - no main window, "
                  "GL context may not be current");
    }
    
    /*
     * Create a dedicated offscreen framebuffer sized exactly to this
     * popup.
     */
    glGenTextures(1, &renderer->texture);
    glBindTexture(GL_TEXTURE_2D, renderer->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    
    glGenFramebuffers(1, &renderer->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, renderer->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                            GL_TEXTURE_2D, renderer->texture, 0);
    
    GLenum fboStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (fboStatus != GL_FRAMEBUFFER_COMPLETE) {
        POPUP_LOG("TkWaylandPopupCreateRenderer: FBO incomplete, status=0x%x",
                   (unsigned int)fboStatus);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &renderer->fbo);
        glDeleteTextures(1, &renderer->texture);
        renderer->fbo = 0;
        renderer->texture = 0;
        free(renderer->pixels);
        free(renderer);
        return NULL;
    }
    
    /* Create a NanoVG context using the GLES3 backend */
    renderer->vg = nvgCreateGLES3(NVG_STENCIL_STROKES | NVG_DEBUG);
    if (!renderer->vg) {
        POPUP_LOG("TkWaylandPopupCreateRenderer: nvgCreateGLES3 failed, trying fallback");
        renderer->vg = nvgCreateGLES3(0);
    }
    
    if (renderer->vg) {
        POPUP_LOG("TkWaylandPopupCreateRenderer: NanoVG context %p created", (void*)renderer->vg);
        
        /* 
         * Load fonts using the Tk font system - NO HARD-CODED PATHS!
         * The fonts will be loaded on demand by EnsureNvgFont.
         */
        TkWaylandLoadNamedFontIntoContext(renderer->vg, "sans");
        TkWaylandLoadNamedFontIntoContext(renderer->vg, "TkDefaultFont");
        TkWaylandLoadNamedFontIntoContext(renderer->vg, "TkMenuFont");
        
        POPUP_LOG("TkWaylandPopupCreateRenderer: fonts registered using Tk font system");
        
        /* Initialize the NanoVG context for the renderer size */
        glViewport(0, 0, width, height);
        nvgBeginFrame(renderer->vg, width, height, 1.0f);
        nvgBeginPath(renderer->vg);
        nvgRect(renderer->vg, 0, 0, width, height);
        nvgFillColor(renderer->vg, nvgRGBA(0, 0, 0, 0));
        nvgFill(renderer->vg);
        nvgEndFrame(renderer->vg);
    } else {
        POPUP_LOG("TkWaylandPopupCreateRenderer: WARNING - no NanoVG context created");
    }
    
    /* Don't leave our FBO bound */
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    
    POPUP_LOG("TkWaylandPopupCreateRenderer: renderer %p created (fbo=%u texture=%u)",
              (void*)renderer, (unsigned int)renderer->fbo, (unsigned int)renderer->texture);
    
    return renderer;
}

/*
 *----------------------------------------------------------------------
 * TkWaylandPopupDestroyRenderer --
 *
 *	Destroy a software renderer and free its resources.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees pixel buffer and NanoVG context.
 *----------------------------------------------------------------------
 */

static void
TkWaylandPopupDestroyRenderer(
    SoftRenderer *renderer)
{
    if (!renderer) return;
    
    POPUP_LOG("TkWaylandPopupDestroyRenderer: destroying renderer %p", (void*)renderer);
    
    if (renderer->vg) {
        /*
         * Must happen before nvgDeleteGLES3(): once the context is
         * freed, its address may be reused by the very next popup's
         * nvgCreateGLES3() call, and any stale EnsureNvgFont() cache
         * entries still pointing at this address would then be
         * mistaken for entries belonging to the new context.
         */
        TkWaylandFontContextDestroyed(renderer->vg);
        nvgDeleteGLES3(renderer->vg);
        renderer->vg = NULL;
    }
    
    if (popupGlobals.mainWindow) {
        glfwMakeContextCurrent(popupGlobals.mainWindow);
    }
    if (renderer->fbo) {
        glDeleteFramebuffers(1, &renderer->fbo);
        renderer->fbo = 0;
    }
    if (renderer->texture) {
        glDeleteTextures(1, &renderer->texture);
        renderer->texture = 0;
    }
    
    if (renderer->pixels) {
        free(renderer->pixels);
        renderer->pixels = NULL;
    }
    
    free(renderer);
}

/*
 *----------------------------------------------------------------------
 * TkWaylandPopupRendererClear --
 *
 *	Clear the renderer's pixel buffer to a solid color.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Fills the entire pixel buffer with the specified color.
 *----------------------------------------------------------------------
 */

static void
TkWaylandPopupRendererClear(
    SoftRenderer *renderer,
    unsigned char r,
    unsigned char g,
    unsigned char b,
    unsigned char a)
{
    if (!renderer || !renderer->pixels) return;
    
    unsigned int color = (a << 24) | (b << 16) | (g << 8) | r;
    unsigned int *pixels = (unsigned int *)renderer->pixels;
    int count = renderer->size / 4;
    
    for (int i = 0; i < count; i++) {
        pixels[i] = color;
    }
    
    renderer->needs_redraw = 1;
}

/*
 *----------------------------------------------------------------------
 * TkWaylandPopupCopyPixelsToBuffer --
 *
 *	Copy the renderer's pixel data to a SHM buffer.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Copies pixel data to the buffer.
 *----------------------------------------------------------------------
 */

static void
TkWaylandPopupCopyPixelsToBuffer(
    TkWaylandPopup *popup,
    WlShmBuffer *buffer)
{
    if (!popup || !popup->renderer || !popup->renderer->pixels || !buffer || !buffer->data) {
        return;
    }
    
    int copy_size = popup->renderer->size;
    if (copy_size > buffer->size) {
        copy_size = buffer->size;
    }
    
    memcpy(buffer->data, popup->renderer->pixels, copy_size);
    POPUP_LOG("TkWaylandPopupCopyPixelsToBuffer: copied %d bytes", copy_size);
}

/*
 *----------------------------------------------------------------------
 * TkWaylandPopupReleaseBuffer --
 *
 *	Callback when the compositor releases a buffer.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Marks the buffer as available for reuse.
 *----------------------------------------------------------------------
 */

static void
TkWaylandPopupReleaseBuffer(
    void *data,
    struct wl_buffer *buffer)
{
    WlShmBuffer *buf = (WlShmBuffer *)data;
    if (buf) {
        buf->in_use = 0;
        POPUP_LOG("TkWaylandPopupReleaseBuffer: released buffer %p", (void*)buf);
    }
    (void)buffer;
}

/*
 *----------------------------------------------------------------------
 * TkWaylandPopupCreateShmBuffer --
 *
 *	Create a wl_shm buffer for the given dimensions.
 *
 * Results:
 *	Pointer to WlShmBuffer or NULL on failure.
 *
 * Side effects:
 *	Allocates shared memory and creates a wl_buffer.
 *----------------------------------------------------------------------
 */

static WlShmBuffer*
TkWaylandPopupCreateShmBuffer(
    struct wl_shm *shm,
    int width,
    int height)
{
    WlShmBuffer *buffer;
    int fd;
    int stride = width * 4;  /* RGBA */
    int size = stride * height;
    void *data;
    struct wl_shm_pool *pool;
    
    if (!shm || width <= 0 || height <= 0) {
        POPUP_LOG("TkWaylandPopupCreateShmBuffer: invalid parameters");
        return NULL;
    }
    
    /* Create a temporary file for shared memory */
    char filename[] = "/tmp/wl-popup-shm-XXXXXX";
    fd = mkstemp(filename);
    if (fd < 0) {
        POPUP_LOG("TkWaylandPopupCreateShmBuffer: mkstemp failed: %s", strerror(errno));
        return NULL;
    }
    
    /* Unlink the file immediately */
    unlink(filename);
    
    /* Set the size of the file */
    if (ftruncate(fd, size) < 0) {
        POPUP_LOG("TkWaylandPopupCreateShmBuffer: ftruncate failed: %s", strerror(errno));
        close(fd);
        return NULL;
    }
    
    /* Map the file into memory */
    data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        POPUP_LOG("TkWaylandPopupCreateShmBuffer: mmap failed: %s", strerror(errno));
        close(fd);
        return NULL;
    }
    
    /* Create a wl_shm_pool */
    pool = wl_shm_create_pool(shm, fd, size);
    if (!pool) {
        POPUP_LOG("TkWaylandPopupCreateShmBuffer: wl_shm_create_pool failed");
        munmap(data, size);
        close(fd);
        return NULL;
    }
    
    /* Create the wl_buffer */
    struct wl_buffer *wl_buf = wl_shm_pool_create_buffer(
        pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
    
    wl_shm_pool_destroy(pool);
    close(fd);
    
    if (!wl_buf) {
        POPUP_LOG("TkWaylandPopupCreateShmBuffer: wl_shm_pool_create_buffer failed");
        munmap(data, size);
        return NULL;
    }
    
    /* Allocate and initialize the buffer structure */
    buffer = (WlShmBuffer *)calloc(1, sizeof(WlShmBuffer));
    if (!buffer) {
        POPUP_LOG("TkWaylandPopupCreateShmBuffer: malloc failed");
        wl_buffer_destroy(wl_buf);
        munmap(data, size);
        return NULL;
    }
    
    buffer->buffer = wl_buf;
    buffer->data = data;
    buffer->width = width;
    buffer->height = height;
    buffer->stride = stride;
    buffer->size = size;
    buffer->in_use = 0;
    
    /* Add buffer listener */
    wl_buffer_add_listener(wl_buf, &buffer_listener, buffer);
    
    POPUP_LOG("TkWaylandPopupCreateShmBuffer: created buffer %p size %dx%d", 
              (void*)buffer, width, height);
    
    return buffer;
}

/*
 *----------------------------------------------------------------------
 * TkWaylandPopupDestroyShmBuffer --
 *
 *	Destroy a wl_shm buffer and free its resources.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees shared memory and destroys wl_buffer.
 *----------------------------------------------------------------------
 */

static void
TkWaylandPopupDestroyShmBuffer(
    WlShmBuffer *buffer)
{
    if (!buffer) return;
    
    POPUP_LOG("TkWaylandPopupDestroyShmBuffer: destroying buffer %p", (void*)buffer);
    
    if (buffer->buffer) {
        wl_buffer_destroy(buffer->buffer);
    }
    if (buffer->data) {
        munmap(buffer->data, buffer->size);
    }
    free(buffer);
}

/*
 *----------------------------------------------------------------------
 * TkWaylandPopupAttachBuffer --
 *
 *	Attach a wl_shm buffer to the popup surface and commit it.
 *
 * Results:
 *	1 on success, 0 on failure.
 *
 * Side effects:
 *	Attaches buffer to surface and marks it as in use.
 *----------------------------------------------------------------------
 */

static int
TkWaylandPopupAttachBuffer(
    TkWaylandPopup *popup,
    WlShmBuffer *buffer)
{
    if (!popup || !popup->surface || !buffer || !buffer->buffer) {
        POPUP_LOG("TkWaylandPopupAttachBuffer: invalid parameters");
        return 0;
    }
    
    /* Attach the buffer to the surface */
    wl_surface_attach(popup->surface, buffer->buffer, 0, 0);
    wl_surface_damage(popup->surface, 0, 0, popup->width, popup->height);
    wl_surface_commit(popup->surface);
    
    buffer->in_use = 1;
    popup->current_buffer = buffer;
    
    POPUP_LOG("TkWaylandPopupAttachBuffer: attached buffer %p to surface %p", 
              (void*)buffer, (void*)popup->surface);
    
    return 1;
}

/*
 *----------------------------------------------------------------------
 * popup_registry_global --
 *
 *	Callback for wl_registry global events.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Binds Wayland globals including wl_shm.
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
    } else if (strcmp(interface, "wl_shm") == 0) {
        popupGlobals.shm = (struct wl_shm *)
            wl_registry_bind(registry, name, &wl_shm_interface,
                version > 1 ? 1 : version);
        POPUP_LOG("Bound wl_shm");
    }
}

/*
 *----------------------------------------------------------------------
 * popup_registry_global_remove --
 *
 *	Callback for wl_registry global remove events.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
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
 * popup_xdg_popup_configure --
 *
 *	Callback when an xdg_popup is configured.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Stores position and size.
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
 * popup_xdg_popup_done --
 *
 *	Callback when an xdg_popup is dismissed by the compositor.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Triggers the done callback if set.
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
 * popup_xdg_surface_configure --
 *
 *	Callback when an xdg_surface is configured.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Acknowledges the configure.
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
    
    popup->pendingConfigure = 1;
    popup->pendingSerial = serial;
    popup->configured = 0;
    
    /* Acknowledge the configure - CRITICAL for surface mapping */
    if (popup->xdgSurface) {
        xdg_surface_ack_configure(popup->xdgSurface, serial);
        popup->configured = 1;
        popup->configureAcked = 1;
        popup->pendingConfigure = 0;
        popup->surfaceMapped = 1;
        POPUP_LOG("xdg_surface configure acknowledged with serial=%u", serial);
    }
    
    /* Commit the surface */
    if (popup->surface) {
        wl_surface_commit(popup->surface);
        POPUP_LOG("xdg_surface surface committed after configure");
    }
}

/*
 *----------------------------------------------------------------------
 * popup_wm_base_ping --
 *
 *	Callback for xdg_wm_base ping.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Replies to the ping.
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
 * TkWaylandPopupGetWLSurface --
 *
 *	Get the wl_surface associated with a GLFW window.
 *
 * Results:
 *	struct wl_surface* or NULL.
 *
 * Side effects:
 *	None.
 *----------------------------------------------------------------------
 */

static struct wl_surface *
TkWaylandPopupGetWLSurface(
    GLFWwindow *window)
{
    if (!window) return NULL;

#ifdef GLFW_EXPOSE_NATIVE_WAYLAND
    struct wl_surface *surf = glfwGetWaylandWindow(window);
    return surf;
#else
    return NULL;
#endif
}

/*
 *----------------------------------------------------------------------
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
        popupGlobals.seat && popupGlobals.shm) {
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
            popupGlobals.wmBase && popupGlobals.seat && popupGlobals.shm) {
            POPUP_LOG("Globals bound from registry");
            return 1;
        }
    }
    
    POPUP_LOG("TkWaylandPopupBindGlobals: failed to bind all globals");
    return 0;
}

/*
 *----------------------------------------------------------------------
 * TkWaylandPopupInit --
 *
 *	Initialize the popup module with global Wayland objects.
 *
 * Results:
 *	1 on success, 0 on failure.
 *
 * Side effects:
 *	Stores Wayland global objects.
 *----------------------------------------------------------------------
 */

MODULE_SCOPE int TkWaylandPopupInit(void) {
    if (popupGlobals.initialized) {
        return 1;
    }
    
    POPUP_LOG("Initializing Wayland SHM popup module");
    
    if (TkWaylandPopupBindGlobals()) {
        popupGlobals.initialized = 1;
        return 1;
    }
    
    return 1;
}

/*
 *----------------------------------------------------------------------
 * TkWaylandPopupSetMainWindow --
 *
 *	Set the main GLFW window for context sharing.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Stores the main window.
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
 * TkWaylandPopupCreate --
 *
 *	Create a new xdg_popup using wl_shm buffers.
 *
 * Results:
 *	Pointer to TkWaylandPopup, or NULL on failure.
 *
 * Side effects:
 *	Creates Wayland objects and wl_shm buffers for the popup.
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
    
    POPUP_LOG("TkWaylandPopupCreate: creating popup at (%d,%d) size %dx%d", 
              anchorX, anchorY, popupW, popupH);
    
    if (!popupGlobals.initialized || !popupGlobals.wmBase || !popupGlobals.shm) {
        POPUP_LOG("TkWaylandPopupCreate: popup module not initialized or missing wl_shm");
        return NULL;
    }
    
    if (!parentGlfw) {
        POPUP_LOG("TkWaylandPopupCreate: no parent window");
        return NULL;
    }
    
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
    popup->pendingConfigure = 0;
    popup->pendingSerial = 0;
    popup->configureAcked = 0;
    popup->surfaceMapped = 0;
    popup->drawing = 0;
    popup->serial = serial;
    popup->current_buffer = NULL;
    popup->pending_buffer = NULL;
    popup->shm = popupGlobals.shm;
    wl_list_init(&popup->buffers);
    popup->buffer_count = 0;
    
    /* Create the software renderer */
    popup->renderer = TkWaylandPopupCreateRenderer(popupW, popupH);
    if (!popup->renderer) {
        POPUP_LOG("TkWaylandPopupCreate: failed to create renderer");
        free(popup);
        xdg_surface_destroy(parentXdgSurface);
        return NULL;
    }
    
    /* Clear the renderer to transparent */
    TkWaylandPopupRendererClear(popup->renderer, 0, 0, 0, 0);
    
    /* Create wl_surface. */
    popup->surface = wl_compositor_create_surface(popupGlobals.compositor);
    if (!popup->surface) {
        POPUP_LOG("TkWaylandPopupCreate: failed to create wl_surface");
        TkWaylandPopupDestroyRenderer(popup->renderer);
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
        TkWaylandPopupDestroyRenderer(popup->renderer);
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
        TkWaylandPopupDestroyRenderer(popup->renderer);
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
    
    /* Create xdg_popup */
    popup->xdgPopup = xdg_surface_get_popup(popup->xdgSurface,
        parentXdgSurface, positioner);
    
    xdg_positioner_destroy(positioner);
    
    if (!popup->xdgPopup) {
        POPUP_LOG("TkWaylandPopupCreate: failed to create xdg_popup");
        xdg_surface_destroy(popup->xdgSurface);
        wl_surface_destroy(popup->surface);
        TkWaylandPopupDestroyRenderer(popup->renderer);
        xdg_surface_destroy(parentXdgSurface);
        free(popup);
        return NULL;
    }
    
    xdg_popup_add_listener(popup->xdgPopup, &popup_xdg_popup_listener, popup);
    
    if (grabInput && serial != 0) {
        xdg_popup_grab(popup->xdgPopup, popupGlobals.seat, serial);
    }
    
    /* Create initial SHM buffers */
    WlShmBuffer *buffer = TkWaylandPopupCreateShmBuffer(
        popupGlobals.shm, popupW, popupH);
    if (!buffer) {
        POPUP_LOG("TkWaylandPopupCreate: failed to create SHM buffer");
        xdg_popup_destroy(popup->xdgPopup);
        xdg_surface_destroy(popup->xdgSurface);
        wl_surface_destroy(popup->surface);
        TkWaylandPopupDestroyRenderer(popup->renderer);
        xdg_surface_destroy(parentXdgSurface);
        free(popup);
        return NULL;
    }
    
    wl_list_insert(&popup->buffers, &buffer->link);
    popup->buffer_count = 1;
    popup->current_buffer = buffer;
    
    /* Copy renderer pixels to buffer */
    TkWaylandPopupCopyPixelsToBuffer(popup, buffer);
    
    /* Attach the buffer to the surface */
    TkWaylandPopupAttachBuffer(popup, buffer);
    
    /* Set input region (empty = no input). */
    popup->inputRegion = wl_compositor_create_region(popupGlobals.compositor);
    if (popup->inputRegion) {
        wl_surface_set_input_region(popup->surface, popup->inputRegion);
        wl_region_destroy(popup->inputRegion);
        popup->inputRegion = NULL;
    }
    
    /* Commit parent surface */
    wl_surface_commit(parentSurface);
    
    POPUP_LOG("TkWaylandPopupCreate: xdg_popup created successfully");
    
    return popup;
}

/*
 *----------------------------------------------------------------------
 * TkWaylandSubsurfaceCreate --
 *
 *	Create a wl_subsurface using wl_shm buffers.
 *
 * Results:
 *	Pointer to TkWaylandPopup, or NULL on failure.
 *
 * Side effects:
 *	Creates Wayland objects and wl_shm buffers for the subsurface.
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
    
    POPUP_LOG("TkWaylandSubsurfaceCreate: creating subsurface at (%d,%d) size %dx%d", 
              x, y, width, height);
    
    if (!popupGlobals.initialized || !popupGlobals.compositor ||
        !popupGlobals.subcompositor || !popupGlobals.shm) {
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
    popup->pendingConfigure = 0;
    popup->pendingSerial = 0;
    popup->configureAcked = 1;
    popup->surfaceMapped = 1;
    popup->drawing = 0;
    popup->current_buffer = NULL;
    popup->pending_buffer = NULL;
    popup->shm = popupGlobals.shm;
    wl_list_init(&popup->buffers);
    popup->buffer_count = 0;
    
    /* Create the software renderer */
    popup->renderer = TkWaylandPopupCreateRenderer(width, height);
    if (!popup->renderer) {
        POPUP_LOG("TkWaylandSubsurfaceCreate: failed to create renderer");
        free(popup);
        return NULL;
    }
    
    /* Clear the renderer to transparent */
    TkWaylandPopupRendererClear(popup->renderer, 0, 0, 0, 0);
    
    /* Create wl_surface. */
    popup->surface = wl_compositor_create_surface(popupGlobals.compositor);
    if (!popup->surface) {
        POPUP_LOG("TkWaylandSubsurfaceCreate: failed to create wl_surface");
        TkWaylandPopupDestroyRenderer(popup->renderer);
        free(popup);
        return NULL;
    }
    
    /* Create wl_subsurface. */
    popup->subsurface = wl_subcompositor_get_subsurface(
        popupGlobals.subcompositor, popup->surface, parentSurface);
    if (!popup->subsurface) {
        POPUP_LOG("TkWaylandSubsurfaceCreate: failed to create subsurface");
        wl_surface_destroy(popup->surface);
        TkWaylandPopupDestroyRenderer(popup->renderer);
        free(popup);
        return NULL;
    }
    
    wl_subsurface_set_position(popup->subsurface, x, y);
    wl_subsurface_set_sync(popup->subsurface);
    
    /* Ensure proper stacking. */
    if (popup->subsurface) {
        wl_subsurface_place_above(popup->subsurface, popup->parentSurface);
        wl_surface_commit(popup->parentSurface);
    }
    
    /* Create SHM buffer */
    WlShmBuffer *buffer = TkWaylandPopupCreateShmBuffer(
        popupGlobals.shm, width, height);
    if (!buffer) {
        POPUP_LOG("TkWaylandSubsurfaceCreate: failed to create SHM buffer");
        wl_subsurface_destroy(popup->subsurface);
        wl_surface_destroy(popup->surface);
        TkWaylandPopupDestroyRenderer(popup->renderer);
        free(popup);
        return NULL;
    }
    
    wl_list_insert(&popup->buffers, &buffer->link);
    popup->buffer_count = 1;
    popup->current_buffer = buffer;
    
    /* Copy renderer pixels to buffer */
    TkWaylandPopupCopyPixelsToBuffer(popup, buffer);
    
    /* Attach the buffer */
    TkWaylandPopupAttachBuffer(popup, buffer);
    
    /* Set input region (empty = no input). */
    popup->inputRegion = wl_compositor_create_region(popupGlobals.compositor);
    if (popup->inputRegion) {
        wl_surface_set_input_region(popup->surface, popup->inputRegion);
        wl_region_destroy(popup->inputRegion);
        popup->inputRegion = NULL;
    }
    
    /* Commit parent surface */
    wl_surface_commit(parentSurface);
    
    POPUP_LOG("TkWaylandSubsurfaceCreate: subsurface created successfully");
    
    return popup;
}

/*
 *----------------------------------------------------------------------
 * TkWaylandPopupDestroy --
 *
 *	Destroy a popup and free all resources.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Destroys Wayland objects and frees SHM buffers.
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandPopupDestroy(
    TkWaylandPopup *popup)
{
    if (!popup) return;
    
    POPUP_LOG("TkWaylandPopupDestroy: destroying popup %p", (void*)popup);
    
    if (popup->drawing) {
        TkWaylandPopupEndDraw(popup);
    }
    
    /* Destroy all SHM buffers */
    WlShmBuffer *buffer, *tmp;
    wl_list_for_each_safe(buffer, tmp, &popup->buffers, link) {
        wl_list_remove(&buffer->link);
        TkWaylandPopupDestroyShmBuffer(buffer);
    }
    popup->buffer_count = 0;
    popup->current_buffer = NULL;
    popup->pending_buffer = NULL;
    
    /* Destroy renderer */
    if (popup->renderer) {
        TkWaylandPopupDestroyRenderer(popup->renderer);
        popup->renderer = NULL;
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
    
    free(popup);
}

/*
 *---------------------------------------------------------------------------
 *
 * TkWaylandPopupBeginDraw --
 *
 *	Prepare the popup for drawing using the NanoVG context.
 *
 * Results:
 *	TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *	Sets up the NanoVG frame for drawing.
 *---------------------------------------------------------------------------
 */

MODULE_SCOPE int
TkWaylandPopupBeginDraw(TkWaylandPopup *popup)
{
    if (!popup) {
        POPUP_LOG("TkWaylandPopupBeginDraw: NULL popup");
        return TCL_ERROR;
    }
    
    if (!popup->renderer) {
        POPUP_LOG("TkWaylandPopupBeginDraw: no renderer");
        return TCL_ERROR;
    }
    
    if (!popup->renderer->vg) {
        POPUP_LOG("TkWaylandPopupBeginDraw: no NanoVG context");
        return TCL_ERROR;
    }
    
    /* Make the GL context current if we have a main window */
    if (popupGlobals.mainWindow) {
        glfwMakeContextCurrent(popupGlobals.mainWindow);
        POPUP_LOG("TkWaylandPopupBeginDraw: made GL context current");
    } else {
        POPUP_LOG("TkWaylandPopupBeginDraw: no main window for context");
        return TCL_ERROR;
    }
    
    /*
     * Bind this popup's own offscreen FBO and size the viewport to
     * match exactly.
     */
    if (!popup->renderer->fbo) {
        POPUP_LOG("TkWaylandPopupBeginDraw: no FBO on renderer");
        return TCL_ERROR;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, popup->renderer->fbo);
    glViewport(0, 0, popup->width, popup->height);
    
    /* Begin the NanoVG frame */
    nvgBeginFrame(popup->renderer->vg, popup->width, popup->height, 1.0f);
    popup->drawing = 1;
    
    POPUP_LOG("TkWaylandPopupBeginDraw: began drawing %dx%d with NanoVG context %p", 
             popup->width, popup->height, (void*)popup->renderer->vg);
    
    return TCL_OK;
}

/*
 *---------------------------------------------------------------------------
 *
 * TkWaylandPopupEndDraw --
 *
 *	Finish drawing and update the popup surface with the SHM buffer.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Ends the NanoVG frame and attaches the buffer to the surface.
 *---------------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandPopupEndDraw(TkWaylandPopup *popup)
{
    WlShmBuffer *buffer;
    
    if (!popup || !popup->drawing) {
        return;
    }
    
    if (popup->renderer && popup->renderer->vg) {
        nvgEndFrame(popup->renderer->vg);
        popup->drawing = 0;
        
        /* Read GL pixels (NanoVG renders upside-down). */
        if (popup->renderer->pixels) {
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, popup->width, popup->height, 
                         GL_RGBA, GL_UNSIGNED_BYTE, 
                         popup->renderer->pixels);
            
            /* Vertical flip */
            unsigned char *temp_row = (unsigned char *)malloc(popup->renderer->stride);
            if (temp_row) {
                for (int y = 0; y < popup->height / 2; y++) {
                    int top = y * popup->renderer->stride;
                    int bottom = (popup->height - 1 - y) * popup->renderer->stride;
                    memcpy(temp_row, popup->renderer->pixels + top, popup->renderer->stride);
                    memcpy(popup->renderer->pixels + top, popup->renderer->pixels + bottom, popup->renderer->stride);
                    memcpy(popup->renderer->pixels + bottom, temp_row, popup->renderer->stride);
                }
                free(temp_row);
            }
        }
        
        /* Unbind our FBO */
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    
    /* Find or create buffer */
    buffer = NULL;
    WlShmBuffer *b;
    wl_list_for_each(b, &popup->buffers, link) {
        if (!b->in_use && b->width == popup->width && b->height == popup->height) {
            buffer = b;
            break;
        }
    }
    
    if (!buffer && popup->shm) {
        buffer = TkWaylandPopupCreateShmBuffer(popup->shm, popup->width, popup->height);
        if (buffer) {
            wl_list_insert(&popup->buffers, &buffer->link);
            popup->buffer_count++;
        }
    }
    
    if (buffer && buffer->data) {
        TkWaylandPopupCopyPixelsToBuffer(popup, buffer);
        
        /* STRONG ATTACH + DAMAGE + COMMIT */
        if (popup->surface) {
            wl_surface_attach(popup->surface, buffer->buffer, 0, 0);
            wl_surface_damage(popup->surface, 0, 0, popup->width, popup->height);
            wl_surface_commit(popup->surface);
            buffer->in_use = 1;
            popup->current_buffer = buffer;
            POPUP_LOG("TkWaylandPopupEndDraw: committed surface with buffer %p", (void*)buffer);
        }
        
        /* Force parent commit for subsurface */
        if (popup->isSubsurface && popup->parentSurface) {
            wl_surface_damage(popup->parentSurface, popup->x, popup->y, popup->width, popup->height);
            wl_surface_commit(popup->parentSurface);
            POPUP_LOG("TkWaylandPopupEndDraw: committed parent surface");
        }
    } else {
        POPUP_LOG("TkWaylandPopupEndDraw: no buffer available");
    }
    
    POPUP_LOG("TkWaylandPopupEndDraw: ended drawing");
}

/*
 *---------------------------------------------------------------------------
 *
 * TkWaylandPopupGetNVGContext --
 *
 *	Return the popup's NanoVG context.
 *
 * Results:
 *	The NVGcontext pointer, or NULL.
 *
 * Side effects:
 *	None.
 *---------------------------------------------------------------------------
 */

MODULE_SCOPE NVGcontext*
TkWaylandPopupGetNVGContext(TkWaylandPopup *popup)
{
    if (!popup || !popup->renderer) {
        return NULL;
    }
    return popup->renderer->vg;
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
    
    if (popup->parentSurface) {
        wl_surface_commit(popup->parentSurface);
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
    
    POPUP_LOG("TkWaylandSubsurfaceReconfigure: new pos=(%d,%d) size=%dx%d", 
              x, y, width, height);
    
    popup->x = x;
    popup->y = y;
    popup->width = width;
    popup->height = height;
    
    /* Recreate renderer with new size */
    if (popup->renderer) {
        TkWaylandPopupDestroyRenderer(popup->renderer);
        popup->renderer = TkWaylandPopupCreateRenderer(width, height);
        if (popup->renderer) {
            TkWaylandPopupRendererClear(popup->renderer, 0, 0, 0, 0);
        }
    }
    
    if (popup->subsurface) {
        wl_subsurface_set_position(popup->subsurface, x, y);
    }
    
    /* Create a new buffer for the new size */
    if (popup->shm && width > 0 && height > 0) {
        WlShmBuffer *buffer = TkWaylandPopupCreateShmBuffer(
            popup->shm, width, height);
        if (buffer) {
            wl_list_insert(&popup->buffers, &buffer->link);
            popup->buffer_count++;
            
            /* Copy renderer pixels to buffer */
            if (popup->renderer) {
                TkWaylandPopupCopyPixelsToBuffer(popup, buffer);
            }
            
            TkWaylandPopupAttachBuffer(popup, buffer);
        }
    }
    
    if (popup->parentSurface) {
        wl_surface_commit(popup->parentSurface);
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
 *	None.
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
 *
 * Results:
 *	TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *	Recreates the popup with new size.
 *---------------------------------------------------------------------------
 */

MODULE_SCOPE int
TkWaylandPopupResize(
    TkWaylandPopup *popup,
    int width,
    int height)
{
    if (!popup || width <= 0 || height <= 0) {
        return TCL_ERROR;
    }
    
    POPUP_LOG("TkWaylandPopupResize: resizing to %dx%d", width, height);
    
    popup->width = width;
    popup->height = height;
    
    /* Recreate renderer with new size */
    if (popup->renderer) {
        TkWaylandPopupDestroyRenderer(popup->renderer);
        popup->renderer = TkWaylandPopupCreateRenderer(width, height);
        if (popup->renderer) {
            TkWaylandPopupRendererClear(popup->renderer, 0, 0, 0, 0);
        }
    }
    
    /* Create a new buffer for the new size */
    if (popup->shm) {
        WlShmBuffer *buffer = TkWaylandPopupCreateShmBuffer(
            popup->shm, width, height);
        if (buffer) {
            wl_list_insert(&popup->buffers, &buffer->link);
            popup->buffer_count++;
            
            /* Copy renderer pixels to buffer */
            if (popup->renderer) {
                TkWaylandPopupCopyPixelsToBuffer(popup, buffer);
            }
            
            TkWaylandPopupAttachBuffer(popup, buffer);
            return TCL_OK;
        }
    }
    
    return TCL_ERROR;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
