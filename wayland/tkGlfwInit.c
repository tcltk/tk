/*
 * tkGlfwInit.c --
 *
 *   GLFW/Wayland-specific interpreter initialization: context
 *   management, window mapping, drawing context lifecycle, color
 *   conversion, and platform init/cleanup. GLFW, NanoVG and libdecor
 *   provide the native platform on which Tk's widget set and event loop
 *   are deployed.
 *
 *
 * Copyright © 1995-1997 Sun Microsystems, Inc.
 * Copyright © 2026  Kevin Walzer
 * Copyright © 2026 Marc Culler
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#define GL_GLEXT_PROTOTYPES
#define NANOVG_GLES3_IMPLEMENTATION

#include "tkInt.h"
#include "tkGlfwInt.h"
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_EGL
#include <GLFW/glfw3native.h>

#define GLFW_EXPOSE_NATIVE_EGL
#include <GLFW/glfw3native.h>

/*
 * Raw Wayland headers for the pointer-serial listener used to support
 * grabbed xdg_popup surfaces (menus, menubuttons, comboboxes).
 */
#define GLFW_EXPOSE_NATIVE_WAYLAND
#include <GLFW/glfw3native.h>
#include <wayland-client.h>


/*
 *----------------------------------------------------------------------
 *
 * Module-level state
 *
 *----------------------------------------------------------------------
 */

/*
 * GLFW requires all initialization and event polling to be done
 * on the main thread.
 */

static int GlfwIsInitialized = 0;

/*
 * The glfwWindow for the root window.
 */

GLFWwindow *mainGlfwWindow;
static TkGlfwContext mainGlfwContext = {0};
static int shutdownInProgress = 0;

/*
 *----------------------------------------------------------------------
 *
 * Raw wl_pointer serial listener
 *
 *	GLFW does not expose the Wayland serial of pointer button events
 *	through its public API, but xdg_popup_grab requires a valid serial
 *	from the most recent button-press event.  We attach our own
 *	wl_pointer listener (alongside GLFW's own, which remains untouched)
 *	purely to record the serial of each button-press event.  The
 *	listener does not call any wl_pointer setter functions, so it
 *	cannot interfere with GLFW's own pointer handling.
 *
 *----------------------------------------------------------------------
 */

/*
 * Last known pointer position, in toplevel-surface-local logical pixels.
 * Updated by PointerMotionTrack.  Menu popups use empty input regions
 * (see TkWaylandSubsurfaceCreate), so the toplevel surface continues to
 * receive all pointer motion/button events -- including while the
 * cursor is visually over a menu -- with coordinates in this same space.
 */
static int lastPointerX = 0;
static int lastPointerY = 0;

static void
PointerEnterStub(
    void *data, struct wl_pointer *pointer, uint32_t serial,
    struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy)
{
    (void)data; (void)pointer; (void)serial; (void)surface;

    lastPointerX = wl_fixed_to_int(sx);
    lastPointerY = wl_fixed_to_int(sy);
}

static void
PointerLeaveStub(
    void *data, struct wl_pointer *pointer, uint32_t serial,
    struct wl_surface *surface)
{
    (void)data; (void)pointer; (void)serial; (void)surface;
}

static void
PointerMotionTrack(
    void *data, struct wl_pointer *pointer, uint32_t time,
    wl_fixed_t sx, wl_fixed_t sy)
{
    (void)data; (void)pointer; (void)time;

    lastPointerX = wl_fixed_to_int(sx);
    lastPointerY = wl_fixed_to_int(sy);

    if (TkWaylandMenuPopupActive()) {
        TkWaylandMenuHandlePointerMotion(lastPointerX, lastPointerY);
    }
}

static void
PointerButtonSerial(
    void *data, struct wl_pointer *pointer, uint32_t serial,
    uint32_t time, uint32_t button, uint32_t state)
{
    (void)data; (void)pointer; (void)time; (void)button;

    if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
        TkWaylandPopupSetSerial(serial);

        if (TkWaylandMenuPopupActive()) {
            TkWaylandMenuHandlePointerButton(lastPointerX, lastPointerY,
                                              (int)button, (int)state);
        }
    } else {
        if (TkWaylandMenuPopupActive()) {
            TkWaylandMenuHandlePointerButton(lastPointerX, lastPointerY,
                                              (int)button, (int)state);
        }
    }
}

static void
PointerAxisStub(
    void *data, struct wl_pointer *pointer, uint32_t time,
    uint32_t axis, wl_fixed_t value)
{
    (void)data; (void)pointer; (void)time; (void)axis; (void)value;
}

static const struct wl_pointer_listener tkPointerSerialListener = {
    PointerEnterStub,
    PointerLeaveStub,
    PointerMotionTrack,
    PointerButtonSerial,
    PointerAxisStub,
};

/*
 *----------------------------------------------------------------------
 *
 * Raw wl_keyboard listener -- Escape-to-dismiss for menu popups
 *
 *	Menu popups created via TkWaylandPostMenuAtAnchor (subsurface-based)
 *	have no xdg_popup grab, so the compositor does not deliver an
 *	implicit "dismiss on Escape" behavior.  We bind our own
 *	wl_keyboard listener (alongside GLFW's) purely to detect the
 *	Escape key (Linux evdev keycode KEY_ESC = 1) while a menu is
 *	posted, and call TkWaylandMenuHandleEscape() to dismiss it.
 *
 *	Like the pointer listener, this does not call any wl_keyboard
 *	setter functions and cannot interfere with GLFW's own keyboard
 *	handling or IBus.
 *
 *----------------------------------------------------------------------
 */

#define TK_WAYLAND_KEY_ESC 1  /* linux/input-event-codes.h: KEY_ESC */

static void
KeyboardKeymapStub(
    void *data, struct wl_keyboard *keyboard, uint32_t format,
    int fd, uint32_t size)
{
    (void)data; (void)keyboard; (void)format; (void)fd; (void)size;
}

static void
KeyboardEnterStub(
    void *data, struct wl_keyboard *keyboard, uint32_t serial,
    struct wl_surface *surface, struct wl_array *keys)
{
    (void)data; (void)keyboard; (void)serial; (void)surface; (void)keys;
}

static void
KeyboardLeaveStub(
    void *data, struct wl_keyboard *keyboard, uint32_t serial,
    struct wl_surface *surface)
{
    (void)data; (void)keyboard; (void)serial; (void)surface;
}

static void
KeyboardKeyEscape(
    void *data, struct wl_keyboard *keyboard, uint32_t serial,
    uint32_t time, uint32_t key, uint32_t state)
{
    (void)data; (void)keyboard; (void)serial; (void)time;

    if (key == TK_WAYLAND_KEY_ESC &&
        state == WL_KEYBOARD_KEY_STATE_PRESSED &&
        TkWaylandMenuPopupActive()) {
        TkWaylandMenuHandleEscape();
    }
}

static void
KeyboardModifiersStub(
    void *data, struct wl_keyboard *keyboard, uint32_t serial,
    uint32_t modsDepressed, uint32_t modsLatched, uint32_t modsLocked,
    uint32_t group)
{
    (void)data; (void)keyboard; (void)serial;
    (void)modsDepressed; (void)modsLatched; (void)modsLocked; (void)group;
}

static void
KeyboardRepeatInfoStub(
    void *data, struct wl_keyboard *keyboard, int32_t rate, int32_t delay)
{
    (void)data; (void)keyboard; (void)rate; (void)delay;
}

static const struct wl_keyboard_listener tkKeyboardEscapeListener = {
    KeyboardKeymapStub,
    KeyboardEnterStub,
    KeyboardLeaveStub,
    KeyboardKeyEscape,
    KeyboardModifiersStub,
    KeyboardRepeatInfoStub,
};

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandRegisterPointerListener --
 *
 *	Attach tkPointerSerialListener to the wl_pointer obtained from the
 *	wl_seat bound by TkWaylandPopupInit().  Must be called after
 *	TkWaylandPopupInit() has run.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Adds a listener to the seat's wl_pointer object.  Wayland dispatches
 *	to listeners in registration order, so this listener's
 *	PointerButtonSerial fires (and stores the serial) before GLFW's own
 *	pointer callback is invoked for the same event.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandRegisterPointerListener(void)
{
    struct wl_seat *seat = TkWaylandPopupGetSeat();
    if (!seat) {
        fprintf(stderr,
            "TkWaylandRegisterPointerListener: no seat available\n");
        return;
    }
    struct wl_pointer *pointer = wl_seat_get_pointer(seat);
    if (!pointer) {
        fprintf(stderr,
            "TkWaylandRegisterPointerListener: seat has no pointer\n");
        return;
    }
    wl_pointer_add_listener(pointer, &tkPointerSerialListener, NULL);
    fprintf(stderr,
        "TkWaylandRegisterPointerListener: serial listener attached\n");

    /*
     * Also attach the Escape-detection keyboard listener.  This shares
     * the same registration-order guarantee: our KeyboardKeyEscape fires
     * before GLFW's own keyboard callback for the same event, and since
     * it calls no wl_keyboard setters it cannot interfere with GLFW or
     * IBus key handling.
     */
    struct wl_keyboard *keyboard = wl_seat_get_keyboard(seat);
    if (!keyboard) {
        fprintf(stderr,
            "TkWaylandRegisterPointerListener: seat has no keyboard\n");
        return;
    }
    wl_keyboard_add_listener(keyboard, &tkKeyboardEscapeListener, NULL);
    fprintf(stderr,
        "TkWaylandRegisterPointerListener: escape listener attached\n");
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandHitTest --
 *
 *	Recursive depth-first hit test over the Tk window tree.  Coordinates
 *	are relative to the toplevel's surface origin (i.e. the same space
 *	as GLFW cursor-position callbacks for that toplevel).
 *
 * Results:
 *	The innermost mapped TkWindow containing (x, y), or NULL.
 *
 *----------------------------------------------------------------------
 */

static TkWindow *
TkWaylandHitTest(
    TkWindow *winPtr,
    int       x,
    int       y)
{
    if (!(winPtr->flags & TK_MAPPED)) {
        return NULL;
    }

    int wx = Tk_X((Tk_Window)winPtr);
    int wy = Tk_Y((Tk_Window)winPtr);
    int ww = Tk_Width((Tk_Window)winPtr);
    int wh = Tk_Height((Tk_Window)winPtr);

    if (x < wx || x >= wx + ww || y < wy || y >= wy + wh) {
        return NULL;
    }

    /* Check children topmost-first (last in the sibling chain is on top). */
    for (TkWindow *childPtr = winPtr->childList;
         childPtr != NULL;
         childPtr = childPtr->nextPtr) {
        TkWindow *hit = TkWaylandHitTest(childPtr, x - wx, y - wy);
        if (hit) {
            return hit;
        }
    }

    return winPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandWindowAtPos --
 *
 *	Find the innermost mapped Tk window under the given coordinates in
 *	a GLFW toplevel's surface space.
 *
 * Results:
 *	The TkWindow under (x, y), or NULL if none.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE TkWindow *
TkWaylandWindowAtPos(
    GLFWwindow *glfwWindow,
    int         x,
    int         y)
{
    TkWindow *toplevel = TkGlfwGetTkWindow(glfwWindow);
    if (!toplevel) return NULL;

    /*
     * The hit test recurses using coordinates relative to each window's
     * own origin; the toplevel's own x,y offset is 0 in its own surface
     * space, so we start the recursion directly with (x, y).
     */
    for (TkWindow *childPtr = toplevel->childList;
         childPtr != NULL;
         childPtr = childPtr->nextPtr) {
        TkWindow *hit = TkWaylandHitTest(childPtr, x, y);
        if (hit) return hit;
    }

    if (x >= 0 && y >= 0 &&
        x < Tk_Width((Tk_Window)toplevel) &&
        y < Tk_Height((Tk_Window)toplevel)) {
        return toplevel;
    }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandPostVirtualEvent --
 *
 *	Queue a virtual event (e.g. "<<MenuDone>>") for the given window on
 *	Tk's normal event queue.  Used by tkWaylandPopup.c consumers (menus,
 *	menubuttons) to defer cleanup from a Wayland protocol callback --
 *	which may run during wl_display_dispatch, outside the normal Tcl
 *	event loop -- to a point where it is safe to manipulate Tk's window
 *	hierarchy.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Queues a TK_VIRTUALEVENT XEvent via Tk_QueueWindowEvent.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandPostVirtualEvent(
    TkWindow   *winPtr,
    const char *eventName)
{
    if (!winPtr) return;

    XVirtualEvent event;
    memset(&event, 0, sizeof(event));

    event.type    = VirtualEvent;
    event.serial  = 0;
    event.send_event = 0;
    event.display = winPtr->display;
    event.event   = Tk_WindowId(winPtr);
    event.root    = XRootWindow(winPtr->display, 0);
    event.subwindow = None;
    event.time    = 0;
    event.x = event.y = 0;
    event.x_root = event.y_root = 0;
    event.state   = 0;
    event.same_screen = 1;
    event.name    = Tk_GetUid(eventName);

    Tk_QueueWindowEvent((XEvent *)&event, TCL_QUEUE_TAIL);
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwCreateBackingStore --
 *
 *	Allocate a new backing store FBO with the specified dimensions.
 *	Creates a color texture, depth+stencil renderbuffer, and a
 *	framebuffer object with both attachments.  All GL resources are
 *	wrapped in a TkGlfwBackingStore structure for safe access by
 *	popup code.
 *
 * Results:
 *	Returns a pointer to the new TkGlfwBackingStore structure, or
 *	NULL if allocation failed.
 *
 * Side effects:
 *	Allocates OpenGL texture, renderbuffer, and framebuffer objects.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE TkGlfwBackingStore *
TkGlfwCreateBackingStore(int width, int height)
{
    TkGlfwBackingStore *store = (TkGlfwBackingStore *)ckalloc(sizeof(TkGlfwBackingStore));
    if (!store) {
        return NULL;
    }

    store->width = width;
    store->height = height;

    /* Generate and bind the Framebuffer Object. */
    glGenFramebuffers(1, &store->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, store->fbo);

    /* Generate, bind, and configure the Color Texture Attachment. */
    glGenTextures(1, &store->colorTex);
    glBindTexture(GL_TEXTURE_2D, store->colorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);


     /* Explicitly set filtering and clamping options on colorTex. */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    /* Attach the color texture map to the frame container. */
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, store->colorTex, 0);
    glGenRenderbuffers(1, &store->depthStencilRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, store->depthStencilRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    
    /* Attach our combined depth/stencil buffer layout to the frame container. */
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, store->depthStencilRbo);

    /* Validate structural stability. */
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    printf("CreateWindow FBO status for .: 0x%x\n", status);
    fflush(stdout);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "CRITICAL ERROR: Failed to finalize FBO backing store mapping. Code: 0x%x\n", status);
        /* Clean up. */
        glDeleteTextures(1, &store->colorTex);
        glDeleteRenderbuffers(1, &store->depthStencilRbo);
        glDeleteFramebuffers(1, &store->fbo);
        ckfree((char *)store);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return NULL;
    }

    /* Unbind and clear context tracking. */
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return store;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwDestroyBackingStore --
 *
 *	Free all OpenGL resources associated with a backing store and
 *	free the structure itself.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Deletes OpenGL framebuffer, texture, and renderbuffer objects.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkGlfwDestroyBackingStore(TkGlfwBackingStore *store)
{
    if (!store) {
        return;
    }
    printf("TkGlfwDestroyBackingStore: destroying FBO %u\n", store->fbo);
    fflush(stdout);

    /* Delete. */
    glDeleteTextures(1, &store->colorTex);
    if (store->depthStencilRbo) {
        glDeleteRenderbuffers(1, &store->depthStencilRbo);
    }
    glDeleteFramebuffers(1, &store->fbo);
    ckfree((char *)store);
}

#if 0
static void GLtest(GLFWwindow *window) {
    int fbWidth = 0, fbHeight = 0;
    glfwGetWindowSize(window, &fbWidth, &fbHeight);
    glfwMakeContextCurrent(window);
    glViewport(0, 0, fbWidth, fbHeight); // Your expected new size
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    // Disable any potential state traps
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_DEPTH_TEST);

    // Draw a solid color screen-filling triangle
    glBegin(GL_TRIANGLES);
    glColor3f(1.0f, 0.0f, 0.0f); // Bright Red
    glVertex2f(-1.0f, -1.0f);    // Bottom-Left
    glVertex2f( 3.0f, -1.0f);    // Far Bottom-Right (extends past screen)
    glVertex2f(-1.0f,  3.0f);    // Far Top-Left (extends past screen)
    glEnd();
}
#endif

/*
 * Buffers for font files needed for window decorations.
 */

static size_t sans_size, bold_size, mono_size;
static unsigned char *sans_data, *bold_data, *mono_data;

static unsigned char* readFont(
    const char* fontPath,
    size_t* size)
{
    size_t fileSize;
    FILE* file = fopen(fontPath, "rb");
    if (!file) {
        fprintf(stderr, "Could not open font file %s\n", fontPath);
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);
    unsigned char* buffer = malloc(fileSize);
    if (!buffer) {
        fclose(file);
        Tcl_Panic("Could not allocate memory for font data.\n");
    }
    if (fileSize != fread(buffer, 1, fileSize, file)) {
	Tcl_Panic("Read failed on font file");
    }
    fclose(file);
    *size = fileSize;
    return buffer;
}

static void freeFonts()
{
    if (sans_data) {
	free(sans_data);
	sans_data = NULL;
    }
    if (bold_data) {
	free(bold_data);
	bold_data = NULL;
    }
    if (mono_data) {
	free(mono_data);
	mono_data = NULL;
    }
}

/*
 *----------------------------------------------------------------------
 * Tk info per GLFWwindow
 *----------------------------------------------------------------------
 */

glfwTkInfo* glfwTkInfoList = NULL;

static glfwTkInfo* createGlfwTkInfo(
    GLFWwindow* glfwWindow,
    TkWindow* winPtr)
{
    glfwTkInfo *infoPtr = Tcl_Alloc(sizeof(glfwTkInfo));
    *infoPtr = (glfwTkInfo) {
	.glfwWindow = glfwWindow,
	.winPtr = winPtr,
	.flags = 0,
	.nextPtr = glfwTkInfoList};
    glfwTkInfoList = infoPtr;

    infoPtr->context.vg = nvgCreateGLES3(NVG_ANTIALIAS
				  | NVG_STENCIL_STROKES
				  | NVG_DEBUG);
    if (!infoPtr->context.vg) {
        fprintf(stderr, "createGlfwTkInfo: nvgCreateGLES3() failed\n");
        glfwDestroyWindow(glfwWindow);
        glfwTerminate();
        return NULL;
    }
    nvgCreateFontMem(infoPtr->context.vg, "sans", sans_data,
		     (int)sans_size, 0);
    nvgCreateFontMem(infoPtr->context.vg, "sans-bold", bold_data,
		     (int)bold_size, 0);
    nvgCreateFontMem(infoPtr->context.vg, "mono", mono_data,
		     (int)mono_size, 0);
    return infoPtr;
}

static void destroyGlfwTkInfo(
    GLFWwindow* glfwWindow)
{
    fprintf(stderr, "destroyGlfwTkInfo\n");
    glfwTkInfo* prev = NULL;
    glfwTkInfo *infoPtr = glfwTkInfoList;
    while(infoPtr) {
	if (infoPtr->glfwWindow == glfwWindow) {
	    if (infoPtr == glfwTkInfoList) {
		glfwTkInfoList = infoPtr->nextPtr;
	    } else {
		prev->nextPtr = infoPtr->nextPtr;
	    }
	    glfwMakeContextCurrent(glfwWindow);
	    glfwSetWindowUserPointer(glfwWindow, NULL);
	    nvgDeleteGLES3(infoPtr->context.vg);
	    Tcl_Free(infoPtr);
	    return;
	}
	prev = infoPtr;
	infoPtr = infoPtr->nextPtr;
    }
    Tcl_Panic("DestroyGlfwTkInfo received unknown window");
}

static glfwTkInfo*
getGlfwTkInfo(
    GLFWwindow *glfwWindow)
{
    for (glfwTkInfo* infoPtr = glfwTkInfoList;
	 infoPtr != NULL;
	 infoPtr = infoPtr->nextPtr) {
	if (infoPtr->glfwWindow == glfwWindow) {
	    return infoPtr;
	}
    }
    Tcl_Panic("GetGlfwTkInfo received unknown window");
}

/*
 *----------------------------------------------------------------------
 *
 * renderFBO --
 *
 * 	This function is called to draw the current contents of the
 * 	backing store framebuffer of a glfwWindow on the screen.  It uses
 * 	glBlitFramebuffer to blit the framebuffer to the back buffer in the
 * 	window's OpenGL context and then calls glfwSwapBuffers to swap the
 * 	back buffer to the screen.  The backing store FBO is left unchanged
 * 	for subsequent drawing functions to modify.
 *
 * Results:
 * 	None.
 *
 * Side effects:
 * 	The current state of the window's backing store framebuffer
 * 	is rendered on the screen.
 *
 *----------------------------------------------------------------------
 */


void
renderFBO(GLFWwindow *window)
{
    glfwTkInfo *infoPtr = glfwGetWindowUserPointer(window);
    if (!infoPtr || !infoPtr->winPtr || !infoPtr->winPtr->privatePtr) {
        return;
    }

    TkWindow *winPtr = infoPtr->winPtr;
    glfwMakeContextCurrent(window);

    if (!winPtr->privatePtr->fb) {
        glClearColor(0.8509f, 0.8509f, 0.8509f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glfwSwapBuffers(window);
        return;
    }

    TkGlfwBackingStore *store = winPtr->privatePtr->fb;

    int fbWidth = 0, fbHeight = 0;
    glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
    if (fbWidth <= 0 || fbHeight <= 0) {
        return;
    }

    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_DEPTH_TEST);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, store->fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

    glBlitFramebuffer(0, 0, store->width, store->height,
                      0, 0, fbWidth, fbHeight,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);

    glfwSwapBuffers(window);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandDisplayAllWindows --
 *
 * 	Called by TkWaylandSetupProc to display any "dirty" windows whose
 * 	backing store framebuffer has been changed by a display proc run by
 * 	Tcl_DoOneEvent since the last call to the SetupProc. The framebuffer
 * 	is blitted to the GL back buffer and then glfwSwapBuffers is called.
 *
 * Results:
 * 	None.
 *
 * Side effects:
 * 	Updates windows on the screen.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandDisplayAllWindows(void)
{
    static int callCount = 0;
    int considered = 0, rendered = 0;
    callCount++;

    for (glfwTkInfo *infoPtr = glfwTkInfoList;
         infoPtr;
         infoPtr = infoPtr->nextPtr) {

        considered++;

        if (!infoPtr->winPtr ||
            !infoPtr->winPtr->privatePtr) {
            continue;
        }

        if (!(infoPtr->flags & needsDisplay)) {
            continue;
        }

        if (infoPtr->flags & dontSwap) {
            continue;
        }

        if (infoPtr->context.nvgFrameActive) {
            nvgEndFrame(infoPtr->context.vg);
            infoPtr->context.nvgFrameActive = 0;
        }

        rendered++;
        renderFBO(infoPtr->glfwWindow);

        infoPtr->flags &= ~needsDisplay;
    }

    if (callCount <= 20) {
        fprintf(stderr, "[DIAG] TkWaylandDisplayAllWindows call #%d: considered=%d rendered=%d\n",
                callCount, considered, rendered);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_ClipDrawableToRect --
 *
 *      There are a number of places in the generic code where a complex
 *      drawing operation is "double-buffered" copying a rectangle in
 *      a window to a pixmap, drawing into the pixmap, and then copying
 *      the pixmap back onto the original screen rectangle.  Platforms
 *      such macOS and Wayland, for which drawing to a window is already
 *      double-buffered can opt out of this behavior by defining
 *      NO_DOUBLE_BUFFERING.  The alternative code first calls this
 *      function with arguments describing the rectangle, then draws
 *      directly to the screen (i.e. to the backing store for the window)
 *      and then calls this function again with an infinite rectangle
 *      having width and height -1.
 *
 *      To make this work correctly in this port we avoid calling
 *      glfwSwapBuffers between the two calls.  In the second call
 *      we blit the rectangle from our backing store framebuffer and
 *      then call glfwSwapBuffers.  We don't bother clipping the
 *      drawing operations.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Calls to glfwSwapBuffers are blocked when a finite rectangle
 *      is passed, and when an infinite rectangle is passed the original
 *      rectangle is blitted to the backing store framebuffer and
 *      glfwSwapBuffers is called.
 *
 *----------------------------------------------------------------------
 */

void
Tk_ClipDrawableToRect(
    TCL_UNUSED(Display *),
    Drawable drawable,
    TCL_UNUSED(int), /* x */
    TCL_UNUSED(int), /* y */
    int width, int height)
{
	
    GLFWwindow *glfwWindow = TkWaylandGetGLFWwindowFromDrawable(drawable);
    if (!glfwWindow) {
        return;
    }

    glfwTkInfo *glfwInfoPtr = glfwGetWindowUserPointer(glfwWindow);
    if (!glfwInfoPtr) {
        return;
    }

    if (width == -1 || height == -1) {
        /* Double buffer segment complete: release lock, flag frame ready. */
        glfwInfoPtr->flags &= ~dontSwap;
        glfwInfoPtr->flags |= needsDisplay;
    } else {
        /* Double buffer segment starting: lock down presentation swaps. */
        glfwInfoPtr->flags |= dontSwap;
    }
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

    fprintf(stderr, "GLFW Error %d: %s\n", error, desc);
}


/*
 *----------------------------------------------------------------------
 *
 * TkGlfwInitialize --
 *
 *	Initializes the GLFW library, and the Wayland protocols.
 *      Creates a GFLWWindow to be used for the root window and its
 *      NanoVG context.
 *
 * Results:
 *	TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *	Initializes GLFW, creates a GFLWwindow for the root,
 *	and its NanoVG context.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE int
TkGlfwInitialize(void)
{
    if (GlfwIsInitialized) return TCL_OK;

    glfwSetErrorCallback(TkGlfwErrorCallback);

#ifdef GLFW_PLATFORM_WAYLAND
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WAYLAND);
#endif

    if (!glfwInit()) {
        fprintf(stderr, "TkGlfwInitialize: glfwInit() failed\n");
        return TCL_ERROR;
    }

    /*
     * The glfwWindow for the Tk root is created here.
     * For all other toplevels the glfwWindow shares the GL context
     * of the root.  The window is created hidden.  It will be
     * shown in Tk_MakeWindow.
     */

    /* Hints apply to the next call to glfwCreateWindow. */
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    glfwWindowHint(GLFW_CONTEXT_CREATION_API,  GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_VISIBLE,               GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE,             GLFW_TRUE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW,         GLFW_TRUE);
    glfwWindowHint(GLFW_AUTO_ICONIFY,          GLFW_FALSE);
    glfwWindowHint(GLFW_SCALE_FRAMEBUFFER,     GLFW_TRUE);
    mainGlfwWindow = glfwCreateWindow(200, 200, "Tk", NULL, NULL);
    if (!mainGlfwWindow) {
        fprintf(stderr, "TkGlfwInitialize: failed to create root window\n");
        glfwTerminate();
        return TCL_ERROR;
    }

    /* Load fonts used in window decorations. */
    sans_data = readFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
	 &sans_size);
    bold_data = readFont("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
	 &bold_size);
    mono_data = readFont("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
	 &mono_size);

    /*
     * A positive swap interval causes glfwSwapBuffers to wait for
     * the end of a display cycle before swapping the buffers.
     */
    glfwMakeContextCurrent(mainGlfwWindow);
    glfwSwapInterval(0);

    /*
     * Initialize the native popup module (binds wl_compositor,
     * xdg_wm_base, wl_seat) and attach the pointer-serial listener.
     * Both require the Wayland display to be open, which it is once
     * mainGlfwWindow has been created above.
     */
    if (TkWaylandPopupInit() != TCL_OK) {
        fprintf(stderr,
            "TkGlfwInitialize: TkWaylandPopupInit failed; "
            "popups/menus will not work\n");
    } else {
        TkWaylandRegisterPointerListener();
    }

    GlfwIsInitialized = 1;
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

    if (!GlfwIsInitialized) {
        shutdownInProgress = 0;
        return;
    }

    /* Tear down any live popup surfaces before destroying GL contexts. */
    TkWaylandPopupDestroyAll();

    /* Delete NanoVG while a context still exists. */
#if 0
    if (mainGlfwContext.vg) {
        /* Make the GL context of the root current if it still exists. */
        if (mainGlfwWindow) {
            glfwMakeContextCurrent(mainGlfwWindow);
            nvgDeleteGLES3(mainGlfwContext.vg);
        }
        mainGlfwContext.vg = NULL;
    }
#endif
    glfwMakeContextCurrent(NULL);
    TkGlfwClearCallbacks(mainGlfwWindow);
    glfwSetErrorCallback(NULL);
    mainGlfwWindow = NULL;
    if (GlfwIsInitialized) {
        glfwTerminate();
        GlfwIsInitialized = 0;
    }
    freeFonts();
    TkWaylandKeyCleanup();
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
 *	Creates a new GLFW window and its associated GlfwTkInfo.
 *
 *----------------------------------------------------------------------
 */
 
MODULE_SCOPE GLFWwindow *
TkGlfwCreateWindow(
    TkWindow   *winPtr,
    int         width,
    int         height,
    const char *title,
    Drawable   *drawableOut)
{
    fprintf(stderr, "[DIAG] TkGlfwCreateWindow: title=%s requested=%dx%d winPtr=%p\n",
            title ? title : "(null)", width, height, (void *)winPtr);
    if (!winPtr) {
        Tcl_Panic("TkGlfwCreateWindow called with null winPtr\n");
    }

    if (shutdownInProgress) return NULL;
    if (!GlfwIsInitialized && TkGlfwInitialize() != TCL_OK) {
        return NULL;
    }

    if (width  <= 1) width  = 200;
    if (height <= 1) height = 200;

    GLFWwindow *glfwWindow = NULL;

    /* Root window uses existing mainGlfwWindow. */
    if (winPtr == (TkWindow *)Tk_MainWindow(winPtr->mainPtr->interp)) {
        fprintf(stderr, "[DIAG] TkGlfwCreateWindow: ROOT branch, resizing mainGlfwWindow to %dx%d\n",
                width, height);
        glfwWindow = mainGlfwWindow;
        glfwSetWindowSize(glfwWindow, width, height);
        glfwSetWindowTitle(glfwWindow, title ? title : "");
    } else {
        fprintf(stderr, "[DIAG] TkGlfwCreateWindow: NON-ROOT branch, creating new window %dx%d\n",
                width, height);
        /* Create a new toplevel. */
        glfwWindowHint(GLFW_CLIENT_API,            GLFW_OPENGL_ES_API);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
        glfwWindowHint(GLFW_CONTEXT_CREATION_API,  GLFW_EGL_CONTEXT_API);
        glfwWindowHint(GLFW_VISIBLE,               GLFW_FALSE);
        glfwWindowHint(GLFW_RESIZABLE,             GLFW_TRUE);
        glfwWindowHint(GLFW_FOCUS_ON_SHOW,         GLFW_TRUE);
        glfwWindowHint(GLFW_AUTO_ICONIFY,          GLFW_FALSE);
        glfwWindowHint(GLFW_SCALE_FRAMEBUFFER,     GLFW_TRUE);

        glfwWindow = glfwCreateWindow(width, height,
                                      title ? title : "",
                                      NULL, mainGlfwWindow);
        if (!glfwWindow) return NULL;

        glfwMakeContextCurrent(glfwWindow);
        glfwSwapInterval(0);
    }

    /* Create Tk/GLFW context wrapper. */
    glfwTkInfo *infoPtr = createGlfwTkInfo(glfwWindow, winPtr);
    fprintf(stderr, "nvgContext for %s is at %p\n",
            Tk_PathName(winPtr), infoPtr);

    if (glfwWindow == mainGlfwWindow) {
        mainGlfwContext = infoPtr->context;
    }

    glfwSetWindowUserPointer(glfwWindow, infoPtr);
    TkGlfwSetupCallbacks(glfwWindow);

    winPtr->privatePtr->glfwWindow = glfwWindow;
    winPtr->changes.width  = width;
    winPtr->changes.height = height;

    /*
     * Allocate the backing store FBO structure.  The framebuffer size
     * callback will create the actual GL resources, but we need the
     * structure allocated now so popup code can safely dereference
     * winPtr->privatePtr->fb.
     */
    if (winPtr->privatePtr) {
        /* Get current framebuffer size from GLFW */
        int fbW = 0, fbH = 0;
        glfwGetFramebufferSize(glfwWindow, &fbW, &fbH);
        fprintf(stderr, "[DIAG] TkGlfwCreateWindow: glfwGetFramebufferSize returned %dx%d (requested was %dx%d)\n",
                fbW, fbH, width, height);
        if (fbW > 0 && fbH > 0) {
            winPtr->privatePtr->fb = TkGlfwCreateBackingStore(fbW, fbH);
        } else {
            /* Fallback: allocate with the requested window size */
            winPtr->privatePtr->fb = TkGlfwCreateBackingStore(width, height);
        }
    }

    /* Pixel ratio logging. */
    float scale;
    glfwGetWindowContentScale(glfwWindow, &scale, NULL);
    fprintf(stderr, "Initial pixel ratio for %s is %f\n",
            Tk_PathName(winPtr), scale);

    /* Return drawable. */
    if (drawableOut) {
        *drawableOut = TkWaylandDrawableForTkWindow(winPtr);
    }

    /*
     * Clear to the Tk background color before first presentation,
     * if the FBO has already been created.
     */
    if (winPtr->privatePtr->fb) {
        TkGlfwBackingStore *store = winPtr->privatePtr->fb;

        glfwMakeContextCurrent(glfwWindow);
        glBindFramebuffer(GL_FRAMEBUFFER, store->fbo);

        glClearColor(
            ((winPtr->atts.background_pixel >> 16) & 0xFF) / 255.0f,
            ((winPtr->atts.background_pixel >>  8) & 0xFF) / 255.0f,
            ( winPtr->atts.background_pixel        & 0xFF) / 255.0f,
            1.0f);

        glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        fprintf(stderr, "CreateWindow FBO status for %s: 0x%x\n",
                Tk_PathName(winPtr), status);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    /* Show window AFTER FBO is (eventually) ready. */
    if (glfwWindow != mainGlfwWindow) {
        fprintf(stderr, "[DIAG] TkGlfwCreateWindow: calling glfwShowWindow (non-root)\n");
        glfwShowWindow(glfwWindow);
    } else {
        fprintf(stderr, "[DIAG] TkGlfwCreateWindow: ROOT window NOT shown here (left hidden)\n");
    }

    /* Present the cleared frame (or first real frame once FBO exists). */
    infoPtr->flags |= needsDisplay;
    renderFBO(glfwWindow);

    fprintf(stderr, "[DIAG] TkGlfwCreateWindow: about to glfwPollEvents (window=%s)\n",
            Tk_PathName(winPtr));
    /* Flush Wayland configure/map. */
    glfwPollEvents();
    fprintf(stderr, "[DIAG] TkGlfwCreateWindow: glfwPollEvents returned\n");

    TkWaylandQueueExposeEvent(winPtr, 0, 0, width, height);

    return glfwWindow;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwDestroyWindow --
 *
 *	Destroy a GLFW window and clean up associated resources.
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
    fprintf(stderr, "TkGlfwDestroyWindow\n");
    if (!glfwWindow) {
	return;
    }
    if (shutdownInProgress) {
	return;
    }

    /* Destroy the backing store FBO before destroying the window. */
    glfwTkInfo *infoPtr = glfwGetWindowUserPointer(glfwWindow);
    if (infoPtr && infoPtr->winPtr && infoPtr->winPtr->privatePtr) {
        if (infoPtr->winPtr->privatePtr->fb) {
            TkGlfwDestroyBackingStore(infoPtr->winPtr->privatePtr->fb);
            infoPtr->winPtr->privatePtr->fb = NULL;
        }
    }

    destroyGlfwTkInfo(glfwWindow);
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
 * TkGlfwBeginDraw --
 *
 *	Prepares a drawing context for rendering into a given Drawable.
 *	Resolves whether the target is an on-screen Window or a rolling
 *	integer ID mapped to a hardware-accelerated Pixmap FBO.
 *	Makes the associated OpenGL context current, binds the correct
 *	framebuffer, and configures NanoVG canvas bounds.
 *
 * Results:
 *	TCL_OK on success, TCL_ERROR if the drawable is invalid, unmapped,
 *	or its context could not be safely resolved.
 *
 * Side effects:
 *	Changes the current OpenGL context, updates active framebuffer
 *	bindings, and populates the fields in dcPtr.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE int
TkGlfwBeginDraw(
    Drawable drawable,
    GC gc,
    TkWaylandDrawingContext *dcPtr)
{
    TkWindow *winPtr;
    TkWindow *topPtr;
    GLFWwindow *glfwWindow;
    glfwTkInfo *infoPtr;
    int fbWidth, fbHeight;
    int winWidth, winHeight;
    float pixelRatio;
    float xOffset = 0.0f;
    float yOffset = 0.0f;

    if (!dcPtr || drawable <= 1) {
        return TCL_ERROR;
    }

    memset(dcPtr, 0, sizeof(*dcPtr));

    /*
     * Pixmap path.
     */

    if (TkWaylandDrawableIsPixmap(drawable)) {
        TkWaylandPixmap *pixmapPtr =
            TkWaylandPixmapFromPixmap((Pixmap)drawable);

        if (!pixmapPtr || !pixmapPtr->fbo || !pixmapPtr->glfwWindow) {
            return TCL_ERROR;
        }

        infoPtr = glfwGetWindowUserPointer(pixmapPtr->glfwWindow);
        if (!infoPtr || !infoPtr->context.vg) {
            return TCL_ERROR;
        }

        glfwMakeContextCurrent(pixmapPtr->glfwWindow);

        glBindFramebuffer(GL_FRAMEBUFFER, pixmapPtr->fbo);
        glViewport(0, 0, pixmapPtr->width, pixmapPtr->height);

        glfwGetWindowSize(
            pixmapPtr->glfwWindow,
            &winWidth,
            &winHeight);

        glfwGetFramebufferSize(
            pixmapPtr->glfwWindow,
            &fbWidth,
            &fbHeight);

        pixelRatio = 1.0f;
        if (winWidth > 0) {
            pixelRatio = (float)fbWidth / (float)winWidth;
        }

        if (!infoPtr->context.nvgFrameActive) {
            nvgBeginFrame(
                infoPtr->context.vg,
                (float)pixmapPtr->width,
                (float)pixmapPtr->height,
                pixelRatio);

            infoPtr->context.nvgFrameActive = 1;
        }

        dcPtr->vg        = infoPtr->context.vg;
        dcPtr->width     = pixmapPtr->width;
        dcPtr->height    = pixmapPtr->height;
        dcPtr->pixmapFbo = pixmapPtr->fbo;
        dcPtr->isPixmap  = 1;

        nvgResetTransform(dcPtr->vg);
        nvgResetScissor(dcPtr->vg);

        return TCL_OK;
    }

    /*
     * Window path.
     */

    winPtr = TkWaylandTkWindowFromDrawable(drawable);
    if (!winPtr) {
        return TCL_ERROR;
    }

    topPtr = winPtr;

    while (topPtr && !Tk_IsTopLevel(topPtr)) {
        xOffset += topPtr->changes.x;
        yOffset += topPtr->changes.y;
        topPtr = topPtr->parentPtr;
    }

    if (!topPtr || !topPtr->privatePtr) {
        return TCL_ERROR;
    }

    glfwWindow = topPtr->privatePtr->glfwWindow;
    if (!glfwWindow) {
        return TCL_ERROR;
    }

    infoPtr = glfwGetWindowUserPointer(glfwWindow);
    if (!infoPtr || !infoPtr->context.vg) {
        return TCL_ERROR;
    }

    glfwMakeContextCurrent(glfwWindow);

    if (topPtr->privatePtr->fb) {
        glBindFramebuffer(
            GL_FRAMEBUFFER,
            topPtr->privatePtr->fb->fbo);
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    glfwGetFramebufferSize(
        glfwWindow,
        &fbWidth,
        &fbHeight);

    if (fbWidth <= 0 || fbHeight <= 0) {
        return TCL_ERROR;
    }

    glfwGetWindowSize(
        glfwWindow,
        &winWidth,
        &winHeight);

    pixelRatio = 1.0f;
    if (winWidth > 0) {
        pixelRatio = (float)fbWidth / (float)winWidth;
    }

    glViewport(0, 0, fbWidth, fbHeight);

    /*
     * IMPORTANT:
     * One NanoVG frame per window repaint.
     */

    if (!infoPtr->context.nvgFrameActive) {

        nvgBeginFrame(
            infoPtr->context.vg,
            (float)winWidth,
            (float)winHeight,
            pixelRatio);

        infoPtr->context.nvgFrameActive = 1;
    }

    nvgResetTransform(infoPtr->context.vg);
    nvgResetScissor(infoPtr->context.vg);

    if (gc) {
        TkGlfwApplyGC(infoPtr->context.vg, gc);
    }

    nvgTranslate(
        infoPtr->context.vg,
        xOffset,
        yOffset);

    dcPtr->vg       = infoPtr->context.vg;
    dcPtr->width    = fbWidth;
    dcPtr->height   = fbHeight;
    dcPtr->winPtr   = winPtr;
    dcPtr->isPixmap = 0;

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwEndDraw --
 *
 *	Concludes a NanoVG drawing transaction on a Wayland window surface.
 *	Unbinds the offscreen backing store FBO. If the window is flagged
 *	as dirty (needsDisplay), this function bypasses event-loop delay
 *	and forces an immediate hardware frame synchronization to resolve
 *	Wayland composition configuration stalls.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Restores the default framebuffer binding. May blit backing FBO
 *	and invoke glfwSwapBuffers immediately on top-level structures.
 *
 *----------------------------------------------------------------------
 */

void
TkGlfwEndDraw(
    TkWaylandDrawingContext *dcPtr)
{
    TkWindow *top;
    glfwTkInfo *info;

    if (!dcPtr) {
        return;
    }

    /*
     * Pixmaps are self-contained.
     */

    if (dcPtr->isPixmap) {
        return;
    }

    if (!dcPtr->winPtr) {
        return;
    }

    top = (TkWindow *)dcPtr->winPtr;

    while (top && !Tk_IsTopLevel(top)) {
        top = top->parentPtr;
    }

    if (!top ||
        !top->privatePtr ||
        !top->privatePtr->glfwWindow) {
        return;
    }

    info = glfwGetWindowUserPointer(
        top->privatePtr->glfwWindow);

    if (!info) {
        return;
    }

    /*
     * Do NOT call nvgEndFrame here.
     *
     * Multiple widgets may still be drawing into the
     * same window during this display cycle.
     */

    info->flags |= needsDisplay;
}
/*
 *----------------------------------------------------------------------
 *
 * TkGlfwGetNVGContext --
 *
 *	Return the NanoVG context for a drawable.
 *
 * Results:
 *	The NVGcontext pointer, or NULL if shutting down.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE NVGcontext*
TkGlfwGetNVGContext(
    Drawable drawable)
{
    if (TkWaylandDrawableIsPixmap(drawable)) {
	fprintf(stderr, "Contexts not available for pixmaps yet.\n");
	return NULL;
    }
    GLFWwindow *glfwWindow = TkWaylandGetGLFWwindowFromDrawable(drawable);
    glfwTkInfo *infoPtr = glfwGetWindowUserPointer(glfwWindow);
    if (!infoPtr || shutdownInProgress) {
	fprintf(stderr, "TkGlfwGetNVContext: No UserPointer\n");
	return NULL;
    }
    return infoPtr->context.vg;
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
 *	Makes the GL context for the root window current.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE NVGcontext *
TkGlfwGetNVGContextForMeasure(void)
{
    if (!GlfwIsInitialized || !mainGlfwContext.vg || shutdownInProgress)
        return NULL;
    glfwMakeContextCurrent(mainGlfwWindow);
    return mainGlfwContext.vg;
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
    TkWaylandKeyInit();
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
 * TkGlfwGetGLFWwindow --
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

MODULE_SCOPE GLFWwindow*
TkWaylandGetGLFWwindow(
    TkWindow *winPtr)
{
    TkWindow *toplevelPtr = winPtr;
    while (!Tk_IsTopLevel(toplevelPtr)) {
	toplevelPtr = toplevelPtr->parentPtr;
    }
    if (toplevelPtr->privatePtr) {
	return toplevelPtr->privatePtr->glfwWindow;
    }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandGetGLFWwindowFromDrawable --
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
TkWaylandGetGLFWwindowFromDrawable(Drawable drawable)
{
    TkWindow *winPtr = TkWaylandTkWindowFromDrawable(drawable);
    return TkWaylandGetGLFWwindow(winPtr);
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

MODULE_SCOPE TkWindow*
TkGlfwGetTkWindow(GLFWwindow *glfwWindow)
{
    glfwTkInfo *infoPtr = glfwGetWindowUserPointer(glfwWindow);
    if (!infoPtr) {
	fprintf(stderr, "TkGlfwGetTkWindow: No UserPointer.\n");
	return NULL;
    }
    if (!infoPtr->winPtr) {
	fprintf(stderr, "TkGlfwGetTkWindow: No winPtr in User data.\n");
	return NULL;
    }

    return infoPtr->winPtr;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
