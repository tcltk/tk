/*
 * tkWaylandXlib.c --
 *
 *	Xlib emulation layer for the Wayland/GLFW/NanoVG Tk port.
 *
 *	Implements the X11 window-management API (XCreateWindow,
 *	XDestroyWindow, XMapWindow, XMoveResizeWindow, XRaiseWindow,
 *	XSetInputFocus, XSetWMName, …) as a thin layer over the
 *	TkGlfw* / TkWayland* API declared in tkGlfwInt.h.
 *
 *
 * Copyright © 1993-1997 The Regents of the University of California /
 *                        Sun Microsystems Inc.
 * Copyright © 2026      Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkGlfwInt.h"
#include <string.h>
#include <stdlib.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/Xlibint.h>
#include <X11/Xresource.h>
#include <X11/Xlocale.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>



/*
 *----------------------------------------------------------------------
 *
 * WindowToGLFW --
 *
 *	Helper function to find a GLFWwindow from an opaque Window handle.
 *	A Window in this port is either:
 *	  (a) a GLFWwindow pointer cast to Window (toplevel), or
 *	  (b) a synthetic child-window ID produced in Tk_MakeWindow.
 *
 * Results:
 *	Pointer to the associated GLFWwindow, or NULL if not found.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static GLFWwindow *
WindowToGLFW(
    Window window)
{
    GLFWwindow *gw;

    if (window == None || window == 0) {
        return NULL;
    }

    /* Try direct drawable lookup first. */
    gw = TkGlfwGetWindowFromDrawable((Drawable)window);
    if (gw != NULL) {
        return gw;
    }

    /* Try interpreting the Window as a GLFWwindow* directly (toplevel path). */
    gw = (GLFWwindow *)window;

    /* Validate by seeing if it is registered. */
    if (TkGlfwGetTkWindow(gw) != NULL) {
        return gw;
    }

    return NULL;
}

/*
 *======================================================================
 *
 * Window Creation and Destruction
 *
 *======================================================================
 */

/*
 *----------------------------------------------------------------------
 *
 * XCreateWindow --
 *
 *	Full Xlib window-creation entry point.
 *	In this port every window ultimately corresponds to a GLFW
 *	window (toplevel) or shares a parent's GLFW window (child).
 *
 * Results:
 *	The new Window handle, or None on failure.
 *
 * Side effects:
 *	Creates a GLFW window when parent is the root window.
 *
 *----------------------------------------------------------------------
 */

Window
XCreateWindow(
    TCL_UNUSED(Display *),
    Window                parent,
    int                   x,
    int                   y,
    unsigned int          width,
    unsigned int          height,
    unsigned int          border_width,
    TCL_UNUSED(int),            /* depth   – handled by GLFW/NanoVG */
    TCL_UNUSED(unsigned int),   /* class   – always InputOutput here */
    TCL_UNUSED(Visual *),       /* visual  – handled by GLFW/NanoVG */
    unsigned long         valuemask,
    XSetWindowAttributes *attributes)
{
    TkGlfwContext *ctx  = TkGlfwGetContext();
    GLFWwindow    *gw   = NULL;
    Drawable       drawable;
    Window         result;

    /* Ensure GLFW is up. */
    if (!ctx->initialized) {
        if (TkGlfwInitialize() != TCL_OK) {
            return None;
        }
    }

    /* Determine whether this is a root-level (toplevel) window. */
    if (parent == None || parent == 0 || parent == 1 /* root */) {
        /*
         * Toplevel: create a real GLFW window.
         * We have no TkWindow* at this call site; pass NULL so
         * TkGlfwCreateWindow generates its own mapping entry.
         */
        int w = (width  > 0) ? (int)width  : 200;
        int h = (height > 0) ? (int)height : 200;

        gw = TkGlfwCreateWindow(NULL, w, h, "", &drawable);
        if (gw == NULL) {
            return None;
        }

        /* Position the window if the caller supplied coordinates. */
        if (x != 0 || y != 0) {
            glfwSetWindowPos(gw, x, y);
        }

        /* Handle a subset of window-attribute hints. */
        if (attributes != NULL) {
            if ((valuemask & CWOverrideRedirect) &&
                attributes->override_redirect) {
                glfwSetWindowAttrib(gw, GLFW_DECORATED, GLFW_FALSE);
            }
            /* CWBackPixel, CWBorderPixel, CWEventMask, etc. are
               recorded by Tk's own attribute machinery; we just
               acknowledge them here. */
        }

        (void)border_width; /* Border drawing is handled by NanoVG. */

        result = (Window)gw;
    } else {
        /*
         * Child window: share the parent's rendering context.
         * Generate a synthetic ID that encodes a relationship to
         * the parent so that WindowToGLFW can walk up if needed.
         */
        result = parent ^ (Window)(uintptr_t)attributes; /* unique-ish */
        if (result == None || result == parent) {
            result = parent + 1;
        }
    }

    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * XCreateSimpleWindow --
 *
 *	Simplified Xlib window-creation entry point.
 *	Delegates to XCreateWindow with a minimal attribute set.
 *
 * Results:
 *	New Window handle, or None on failure.
 *
 * Side effects:
 *	See XCreateWindow.
 *
 *----------------------------------------------------------------------
 */

Window
XCreateSimpleWindow(
    Display      *display,
    Window        parent,
    int           x,
    int           y,
    unsigned int  width,
    unsigned int  height,
    unsigned int  border_width,
    unsigned long border,
    unsigned long background)
{
    XSetWindowAttributes attr;
    unsigned long         mask = CWBackPixel | CWBorderPixel;

    attr.background_pixel = background;
    attr.border_pixel     = border;

    return XCreateWindow(display, parent,
                         x, y, width, height, border_width,
                         CopyFromParent, InputOutput, CopyFromParent,
                         mask, &attr);
}

/*
 *----------------------------------------------------------------------
 *
 * XDestroyWindow --
 *
 *	Destroy a window and all its subwindows.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	Destroys the GLFW window when found.
 *
 *----------------------------------------------------------------------
 */

int
XDestroyWindow(
    TCL_UNUSED(Display *),
    Window window)
{
    GLFWwindow *gw = WindowToGLFW(window);

    if (gw != NULL) {
        TkGlfwDestroyWindow(gw);
    }

    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XDestroySubwindows --
 *
 *	Destroy all direct subwindows of window.
 *	In this port child windows do not own independent GLFW windows,
 *	so this is a no-op that still returns Success for compatibility.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	None (child windows share the parent's GLFW window).
 *
 *----------------------------------------------------------------------
 */

int
XDestroySubwindows(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window))
{
    /* Child windows share the parent GLFW context – nothing to destroy. */
    return Success;
}

/*
 *======================================================================
 *
 * Window Mapping / Visibility
 *
 *======================================================================
 */

/*
 *----------------------------------------------------------------------
 *
 * XMapWindow --
 *
 *	Make a window visible.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	Shows the GLFW window.
 *
 *----------------------------------------------------------------------
 */

int
XMapWindow(
    TCL_UNUSED(Display *),
    Window window)
{
    GLFWwindow *gw = WindowToGLFW(window);

    if (gw != NULL) {
        glfwShowWindow(gw);
    }

    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XMapRaised --
 *
 *	Make a window visible and raise it to the top of the stack.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	Shows and focuses the GLFW window.
 *
 *----------------------------------------------------------------------
 */

int
XMapRaised(
    TCL_UNUSED(Display *),
    Window window)
{
    GLFWwindow *gw = WindowToGLFW(window);

    if (gw != NULL) {
        glfwShowWindow(gw);
        glfwFocusWindow(gw);
    }

    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XMapSubwindows --
 *
 *	Map all unmapped subwindows.
 *	Child windows share the parent's GLFW window and are always
 *	"visible" in the compositing sense; this is therefore a no-op.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XMapSubwindows(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window))
{
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XUnmapWindow --
 *
 *	Hide a window.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	Hides the GLFW window.
 *
 *----------------------------------------------------------------------
 */

int
XUnmapWindow(
    TCL_UNUSED(Display *),
    Window window)
{
    GLFWwindow *gw = WindowToGLFW(window);

    if (gw != NULL) {
        glfwHideWindow(gw);
    }

    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XUnmapSubwindows --
 *
 *	Unmap all mapped subwindows.  No-op for the same reason as
 *	XMapSubwindows.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XUnmapSubwindows(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window))
{
    return Success;
}

/*
 *======================================================================
 *
 * Window Configuration (position, size, border)
 *
 *======================================================================
 */

/*
 *----------------------------------------------------------------------
 *
 * XResizeWindow --
 *
 *	Change the size of a window.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	Resizes the GLFW window.
 *
 *----------------------------------------------------------------------
 */

int
XResizeWindow(
    TCL_UNUSED(Display *),
    Window       window,
    unsigned int width,
    unsigned int height)
{
    GLFWwindow *gw = WindowToGLFW(window);

    if (gw != NULL) {
        glfwSetWindowSize(gw, (int)width, (int)height);
        TkGlfwUpdateWindowSize(gw, (int)width, (int)height);
    }

    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XMoveWindow --
 *
 *	Change the position of a window.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	Repositions the GLFW window.
 *
 *----------------------------------------------------------------------
 */

int
XMoveWindow(
    TCL_UNUSED(Display *),
    Window window,
    int    x,
    int    y)
{
    GLFWwindow *gw = WindowToGLFW(window);

    if (gw != NULL) {
        glfwSetWindowPos(gw, x, y);
    }

    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XMoveResizeWindow --
 *
 *	Change position and size atomically.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	Repositions and resizes the GLFW window.
 *
 *----------------------------------------------------------------------
 */

int
XMoveResizeWindow(
    TCL_UNUSED(Display *),
    Window       window,
    int          x,
    int          y,
    unsigned int width,
    unsigned int height)
{
    GLFWwindow *gw = WindowToGLFW(window);

    if (gw != NULL) {
        glfwSetWindowPos(gw,  x, y);
        glfwSetWindowSize(gw, (int)width, (int)height);
        TkGlfwUpdateWindowSize(gw, (int)width, (int)height);
    }

    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XConfigureWindow --
 *
 *	General-purpose window configuration.
 *	Handles CWX, CWY, CWWidth, CWHeight, CWBorderWidth from the
 *	value_mask; stacking-related bits (CWSibling, CWStackMode) are
 *	silently ignored because the Wayland compositor controls the
 *	window stack.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	May move and/or resize the GLFW window.
 *
 *----------------------------------------------------------------------
 */

int
XConfigureWindow(
    TCL_UNUSED(Display *),
    Window          window,
    unsigned int    value_mask,
    XWindowChanges *values)
{
    GLFWwindow *gw = WindowToGLFW(window);
    int         x  = -1, y  = -1;
    int         w  = -1, h  = -1;
    int         moveNeeded   = 0;
    int         resizeNeeded = 0;

    if (gw == NULL || values == NULL) {
        return Success;
    }

    /* Collect the current GLFW state to fill in un-specified fields. */
    glfwGetWindowPos(gw,  &x, &y);
    glfwGetWindowSize(gw, &w, &h);

    if (value_mask & CWX) { x = values->x; moveNeeded   = 1; }
    if (value_mask & CWY) { y = values->y; moveNeeded   = 1; }

    if (value_mask & CWWidth)  { w = values->width;  resizeNeeded = 1; }
    if (value_mask & CWHeight) { h = values->height; resizeNeeded = 1; }

    /* CWBorderWidth: recorded for Tk bookkeeping; no GLFW equivalent. */
    /* CWSibling / CWStackMode: compositor-controlled; ignore. */

    if (moveNeeded) {
        glfwSetWindowPos(gw, x, y);
    }
    if (resizeNeeded) {
        glfwSetWindowSize(gw, w, h);
        TkGlfwUpdateWindowSize(gw, w, h);
    }

    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XSetWindowBorderWidth --
 *
 *	Change a window's border width.
 *	NanoVG handles border drawing; the GLFW border is the window
 *	decoration managed by the compositor.  We accept this call for
 *	Xlib compatibility but take no action.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XSetWindowBorderWidth(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(unsigned int))
{
    /* Border drawing is done by NanoVG / the compositor. */
    return Success;
}

/*
 *======================================================================
 *
 * Window stacking Order
 *
 *======================================================================
 */

/*
 *----------------------------------------------------------------------
 *
 * XRaiseWindow --
 *
 *	Raise a window to the top of the stack.
 *	GLFW exposes glfwFocusWindow which brings the window to the
 *	front on most Wayland compositors.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	Focuses / raises the GLFW window.
 *
 *----------------------------------------------------------------------
 */

int
XRaiseWindow(
    TCL_UNUSED(Display *),
    Window window)
{
    GLFWwindow *gw = WindowToGLFW(window);

    if (gw != NULL) {
        glfwFocusWindow(gw);
    }

    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XLowerWindow --
 *
 *	Lower a window to the bottom of the stack.
 *	Wayland compositors do not expose a portable "lower" operation.
 *	We accept the call and return Success for compatibility.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XLowerWindow(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window))
{
    /* No-op: the compositor controls window stacking in Wayland. */
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XCirculateSubwindowsUp --
 *
 *	Raise the bottom-most subwindow to the top.
 *	No-op in Wayland.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XCirculateSubwindowsUp(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window))
{
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XCirculateSubwindowsDown --
 *
 *	Lower the top-most subwindow to the bottom.
 *	No-op in Wayland.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XCirculateSubwindowsDown(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window))
{
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XRestackWindows --
 *
 *	Restack multiple windows.
 *	The Wayland compositor owns the global window stack; individual
 *	applications cannot reorder top-level surfaces relative to each
 *	other.  We attempt to raise each window in the given order
 *	(best-effort) and return Success.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	May focus the first window in the array.
 *
 *----------------------------------------------------------------------
 */

int
XRestackWindows(
    TCL_UNUSED(Display *),
    Window *windows,
    int     nwindows)
{
    int i;

    if (windows == NULL || nwindows <= 0) {
        return Success;
    }

    /* Raise each window in order; the compositor may or may not honor this. */
    for (i = 0; i < nwindows; i++) {
        GLFWwindow *gw = WindowToGLFW(windows[i]);
        if (gw != NULL) {
            glfwFocusWindow(gw);
        }
    }

    return Success;
}

/*
 *======================================================================
 *
 * Window Attributes and Other Management
 *
 *======================================================================
 */

/*
 *----------------------------------------------------------------------
 *
 * XChangeWindowAttributes --
 *
 *	Change one or more window attributes.
 *	We handle override-redirect (GLFW DECORATED hint) and the
 *	always-on-top semantic (GLFW FLOATING hint).  Other attributes
 *	such as background pixel, event mask, cursor, etc. are accepted
 *	silently; they are managed by Tk's own machinery or are not
 *	meaningful in Wayland.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	May change GLFW window attributes.
 *
 *----------------------------------------------------------------------
 */

int
XChangeWindowAttributes(
    TCL_UNUSED(Display *),
    Window                window,
    unsigned long         valuemask,
    XSetWindowAttributes *attributes)
{
    GLFWwindow *gw;

    if (attributes == NULL) {
        return Success;
    }

    gw = WindowToGLFW(window);
    if (gw == NULL) {
        return Success;
    }

    if (valuemask & CWOverrideRedirect) {
        glfwSetWindowAttrib(gw, GLFW_DECORATED,
            attributes->override_redirect ? GLFW_FALSE : GLFW_TRUE);
    }

    /* CWBackPixel, CWBorderPixel, CWEventMask, CWColormap, CWCursor …
       All are maintained by Tk's own attribute tables; no GLFW action. */

    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XSetWindowBackground --
 *
 *	Set the window background pixel.
 *	Background painting is done by NanoVG during drawing; we store
 *	no per-window background in GLFW.  Accept and return Success.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XSetWindowBackground(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(unsigned long))
{
    /* Background is drawn via NanoVG; no GLFW action needed. */
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XSetWindowBackgroundPixmap --
 *
 *	Set the window background from a pixmap.
 *	Accepts ParentRelative and None values per Xlib semantics; actual
 *	background rendering goes through NanoVG.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XSetWindowBackgroundPixmap(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(Pixmap))
{
    /* No-op; background is drawn via NanoVG. */
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XSetWindowBorder --
 *
 *	Set the border colour of a window.
 *	Borders are drawn by NanoVG; this call is a no-op.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XSetWindowBorder(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(unsigned long))
{
    /* Border painting is done via NanoVG. */
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XSetWindowBorderPixmap --
 *
 *	Set the border from a pixmap.  No-op.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XSetWindowBorderPixmap(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(Pixmap))
{
    /* Border painting is done via NanoVG. */
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XSetInputFocus --
 *
 *	Set keyboard input focus to a window.
 *	GLFW's glfwFocusWindow requests focus from the compositor; the
 *	Wayland protocol makes no guarantee that the compositor will
 *	honour the request.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	Requests GLFW window focus.
 *
 *----------------------------------------------------------------------
 */

int
XSetInputFocus(
    TCL_UNUSED(Display *),
    Window focus,
    TCL_UNUSED(int),    /* revert_to */
    TCL_UNUSED(Time))   /* time      */
{
    GLFWwindow *gw;

    if (focus == None || focus == PointerRoot) {
        return Success;
    }

    gw = WindowToGLFW(focus);
    if (gw != NULL) {
        glfwFocusWindow(gw);
    }

    return Success;
}

/*
 *======================================================================
 *
 * ICCCM Text Properties (Window Title / Icon Name)
 *
 *======================================================================
 */

/*
 *----------------------------------------------------------------------
 *
 * XSetWMName --
 *
 *	Set the WM_NAME property (window title) via an XTextProperty.
 *	We decode the text value and forward it to GLFW.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the GLFW window title.
 *
 *----------------------------------------------------------------------
 */

void
XSetWMName(
    TCL_UNUSED(Display *),
    Window        window,
    XTextProperty *text_prop)
{
    GLFWwindow *gw;
    const char *title;

    if (text_prop == NULL || text_prop->value == NULL) {
        return;
    }

    gw = WindowToGLFW(window);
    if (gw == NULL) {
        return;
    }

    title = (const char *)text_prop->value;
    glfwSetWindowTitle(gw, title);
}

/*
 *----------------------------------------------------------------------
 *
 * XSetWMIconName --
 *
 *	Set the WM_ICON_NAME property.
 *	Wayland compositors do not expose a portable icon-name API;
 *	we accept the call for ICCCM compliance and do nothing.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
XSetWMIconName(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(XTextProperty *))
{
    /* Icon names are not exposed via Wayland protocols; no-op. */
}

/*
 *======================================================================
 *
 *  Atom Stubs
 *
 *	These stubs are consolidated here so that every Xlib
 *	compatibility symbol resides in the emulation layer.
 *
 *======================================================================
 */

/*
 *----------------------------------------------------------------------
 *
 * XInternAtom --
 *
 *	Intern an atom name.
 *
 * Results:
 *	A synthesized atom value (incremented static counter).
 *
 * Side effects:
 *	Increments the fakeAtom counter.
 *
 *----------------------------------------------------------------------
 */

Atom
XInternAtom(
    TCL_UNUSED(Display *),
    TCL_UNUSED(const char *),
    TCL_UNUSED(Bool))
{
    static Atom fakeAtom = 1;
    return fakeAtom++;
}

/*
 *----------------------------------------------------------------------
 *
 * XGetAtomName --
 *
 *	Get the name of an atom.
 *
 * Results:
 *	Always returns NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

char *
XGetAtomName(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Atom))
{
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * XGetWindowProperty --
 *
 *	Get a window property.
 *
 * Results:
 *	Success, with all return values set to None/NULL/0.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XGetWindowProperty(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(Atom),
    TCL_UNUSED(long),
    TCL_UNUSED(long),
    TCL_UNUSED(Bool),
    TCL_UNUSED(Atom),
    Atom          *actual_type_return,
    int           *actual_format_return,
    unsigned long *nitems_return,
    unsigned long *bytes_after_return,
    unsigned char **prop_return)
{
    *actual_type_return   = None;
    *actual_format_return = 0;
    *nitems_return        = 0;
    *bytes_after_return   = 0;
    *prop_return          = NULL;
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XResourceManagerString --
 *
 *	Get the resource manager string.
 *
 * Results:
 *	Always returns NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

char *
XResourceManagerString(
    TCL_UNUSED(Display *))
{
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * XFree --
 *
 *	Free memory allocated by Xlib functions.
 *
 * Results:
 *	Always returns 0.
 *
 * Side effects:
 *	None (memory is not freed by this stub).
 *
 *----------------------------------------------------------------------
 */

int
XFree(
    TCL_UNUSED(void *))
{
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpScanWindowId --
 *
 *	Scan a string for a window ID.
 *
 * Results:
 *	TCL_OK if conversion succeeded, TCL_ERROR otherwise.
 *
 * Side effects:
 *	Sets *idPtr to the converted value.
 *
 *----------------------------------------------------------------------
 */

int
TkpScanWindowId(
    Tcl_Interp *interp,
    const char *string,
    Window     *idPtr)
{
    Tcl_Obj obj;

    obj.refCount = 1;
    obj.bytes    = (char *)string;
    obj.length   = strlen(string);
    obj.typePtr  = NULL;

    return Tcl_GetLongFromObj(interp, &obj, (long *)idPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * TkUnixDoOneXEvent --
 *
 *	Process one X event.  No-op in Wayland port.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

bool
TkUnixDoOneXEvent(
    TCL_UNUSED(Tcl_Time *))  /* timePtr */
{
    /* No-op. */
    return false;
}

/*
 *----------------------------------------------------------------------
 *
 * TkCreateXEventSource --
 *
 *	Create X event source.  No-op in Wayland port.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
TkCreateXEventSource(void)
{
    /* No-op. */
}

/*
 *----------------------------------------------------------------------
 *
 * TkClipCleanup --
 *
 *	Clean up clip resources.  No-op in Wayland port.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
TkClipCleanup(
    TCL_UNUSED(TkDisplay *))
{
    /* No-op. */
}

/*
 *----------------------------------------------------------------------
 *
 * TkUnixSetMenubar --
 *
 *	Set the menubar for a window.  No-op in Wayland port.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
TkUnixSetMenubar(
    TCL_UNUSED(Tk_Window),
    TCL_UNUSED(Tk_Window))
{
    /* No-op. */
}


/*
 *----------------------------------------------------------------------
 *
 * Tk_SetMainMenubar --
 *
 *	Set the main menubar.  No-op in Wayland port.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
Tk_SetMainMenubar(
    TCL_UNUSED(Tcl_Interp *),
    TCL_UNUSED(Tk_Window),
    TCL_UNUSED(const char *))
{
    /* No-op. */
}

/*
 *----------------------------------------------------------------------
 *
 * TkpSync --
 *
 *	Syncs displays.  No-op in Wayland port.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */


void
TkpSync(
    TCL_UNUSED(Display *))	/* Display to sync. */
{
    /* No-op */
}

/*
 *----------------------------------------------------------------------
 *
 * XOpenDisplay --
 *
 *	Connect to X server and build internal Display.  No-op in Wayland port.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
 
 /* Opaque no-op display structure. */
typedef struct {
	int no_op; 
} NoOpDisplay;


Display *
XOpenDisplay(TCL_UNUSED(const char *)) /* display_name */
{
    static NoOpDisplay d = {0};
    return (Display *)&d;
}

/*
 *----------------------------------------------------------------------
 *
 * XCloseDisplay --
 *
 *	Tear down X connection and free internals.  No-op in Wayland port.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XCloseDisplay(TCL_UNUSED(Display *))
{ 
   return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * XSync --
 *
 *	Flush X resources.  No-op in Wayland port.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XSync(TCL_UNUSED(Display *), /* display */
	TCL_UNUSED(Bool))  /* discard */
{
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XReparentWindow --
 *
 *	Reparent a window. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XReparentWindow(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(Window),
    TCL_UNUSED(int),
    TCL_UNUSED(int))
{
    /* No-op - window reparenting not supported in Wayland. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XSetIMValues --
 *
 *	Set input method values. No-op in Wayland port.
 *
 * Results:
 *	Always returns NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

char*
XSetIMValues(
    TCL_UNUSED(XIM),
    ...)
{
    /* No-op - input method not used in Wayland port. */
    return NULL;
}
/*
 *----------------------------------------------------------------------
 *
 * XSetFillStyle --
 *
 *	Set GC fill style. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XSetFillStyle(
    TCL_UNUSED(Display *),
    TCL_UNUSED(GC),
    TCL_UNUSED(int))
{
    /* No-op - fill style handled by NanoVG. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XQueryColors --
 *
 *	Query color values from colormap. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XQueryColors(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Colormap),
    TCL_UNUSED(XColor *),
    TCL_UNUSED(int))
{
    /* No-op - color management handled by NanoVG. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XNoOp --
 *
 *	Perform no operation. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XNoOp(
    TCL_UNUSED(Display *))
{
    /* No-op - literally does nothing. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XFreeColormap --
 *
 *	Free a colormap. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XFreeColormap(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Colormap))
{
    /* No-op - colormaps not used in Wayland port. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XGContextFromGC --
 *
 *	Get GContext from GC. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

GContext
XGContextFromGC(
    TCL_UNUSED(GC))
{
    /* No-op - GContext not used in Wayland port. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XSetFillRule --
 *
 *	Set GC fill rule. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XSetFillRule(
    TCL_UNUSED(Display *),
    TCL_UNUSED(GC),
    TCL_UNUSED(int))
{
    /* No-op - fill rule handled by NanoVG. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XSendEvent --
 *
 *	Send an event to a window. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XSendEvent(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(int),
    TCL_UNUSED(long),
    TCL_UNUSED(XEvent *))
{
    /* No-op - event sending not supported in Wayland. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XWindowEvent --
 *
 *	Wait for a specific window event. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XWindowEvent(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(long),
    TCL_UNUSED(XEvent *))
{
    /* No-op - event handling via GLFW. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XGetIMValues --
 *
 *	Get input method values. No-op in Wayland port.
 *
 * Results:
 *	Always returns NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

char*
XGetIMValues(
    TCL_UNUSED(XIM),
    ...)
{
    /* No-op - input method not used in Wayland port. */
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * XSetErrorHandler --
 *
 *	Set Xlib error handler. No-op in Wayland port.
 *
 * Results:
 *	Always returns NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

XErrorHandler
XSetErrorHandler(
    TCL_UNUSED(XErrorHandler))
{
    /* No-op - X errors not generated in Wayland port. */
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * XSynchronize --
 *
 *	Set synchronization mode. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int (*XSynchronize(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Bool)))(Display *)
{
    /* No-op - synchronization not applicable to Wayland. */
    return (int (*)(Display *))XSynchronize;
}

/*
 *----------------------------------------------------------------------
 *
 * XGrabPointer --
 *
 *	Grab the pointer. No-op in Wayland port.
 *
 * Results:
 *	Always returns GrabSuccess.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XGrabPointer(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(Bool),
    TCL_UNUSED(unsigned int),
    TCL_UNUSED(int),
    TCL_UNUSED(int),
    TCL_UNUSED(Window),
    TCL_UNUSED(Cursor),
    TCL_UNUSED(Time))
{
    /* No-op - pointer grabbing not supported in Wayland. */
    return GrabSuccess;
}

/*
 *----------------------------------------------------------------------
 *
 * XForceScreenSaver --
 *
 *	Force screensaver activation. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XForceScreenSaver(
    TCL_UNUSED(Display *),
    TCL_UNUSED(int))
{
    /* No-op - screensaver control not supported in Wayland. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XClipBox --
 *
 *	Get bounding box of region. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0, with rect_return zeroed if non-NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XClipBox(
    TCL_UNUSED(Region),
    XRectangle *rect_return)
{
    /* No-op - regions not used in Wayland port. */
    if (rect_return) {
        rect_return->x = rect_return->y = rect_return->width = rect_return->height = 0;
    }
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XQueryTree --
 *
 *	Query window tree. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0, with output parameters set to NULL/0.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XQueryTree(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(Window *),
    TCL_UNUSED(Window *),
    Window **children_return,
    unsigned int *nchildren_return)
{
    /* No-op - window tree not maintained in Wayland port. */
    if (children_return) {
        *children_return = NULL;
    }
    if (nchildren_return) {
        *nchildren_return = 0;
    }
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XkbKeycodeToKeysym --
 *
 *	Convert keycode to keysym. No-op in Wayland port.
 *
 * Results:
 *	Always returns NoSymbol.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

KeySym
XkbKeycodeToKeysym(
    TCL_UNUSED(Display *),
    unsigned int kc,  /* Note: unsigned int, not KeyCode */
    TCL_UNUSED(int),
    TCL_UNUSED(int))
{
    /* No-op - keyboard mapping handled by GLFW. */
    (void)kc;  /* Suppress unused parameter warning */
    return NoSymbol;
}

/*
 *----------------------------------------------------------------------
 *
 * XPointInRegion --
 *
 *	Test if point is in region. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (False).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XPointInRegion(
    TCL_UNUSED(Region),
    TCL_UNUSED(int),
    TCL_UNUSED(int))
{
    /* No-op - regions not used in Wayland port. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XSetFont --
 *
 *	Set GC font. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XSetFont(
    TCL_UNUSED(Display *),
    TCL_UNUSED(GC),
    TCL_UNUSED(Font))
{
    /* No-op - font handling via NanoVG. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XFreeFontSet --
 *
 *	Free a font set. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
XFreeFontSet(
    TCL_UNUSED(Display *),
    TCL_UNUSED(XFontSet))
{
    /* No-op - font sets not used in Wayland port. */
    return;
}

/*
 *----------------------------------------------------------------------
 *
 * XFilterEvent --
 *
 *	Filter an event. No-op in Wayland port.
 *
 * Results:
 *	Always returns False.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Bool
XFilterEvent(
    TCL_UNUSED(XEvent *),
    TCL_UNUSED(Window))
{
    /* No-op - event filtering handled by GLFW. */
    return False;
}

/*
 *----------------------------------------------------------------------
 *
 * XIconifyWindow --
 *
 *	Iconify (minimize) a window. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XIconifyWindow(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(int))
{
    /* No-op - window iconification handled by compositor. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XUngrabServer --
 *
 *	Ungrab the X server. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XUngrabServer(
    TCL_UNUSED(Display *))
{
    /* No-op - server grabbing not applicable to Wayland. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XKeycodeToKeysym --
 *
 *	Convert keycode to keysym (legacy). No-op in Wayland port.
 *
 * Results:
 *	Always returns NoSymbol.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

KeySym
XKeycodeToKeysym(
    TCL_UNUSED(Display *),
    unsigned int kc,  /* Note: unsigned int, not KeyCode */
    TCL_UNUSED(int))
{
    /* No-op - keyboard mapping handled by GLFW. */
    (void)kc;  /* Suppress unused parameter warning */
    return NoSymbol;
}

/*
 *----------------------------------------------------------------------
 *
 * XRegisterIMInstantiateCallback --
 *
 *	Register input method instantiation callback. No-op in Wayland port.
 *
 * Results:
 *	Always returns False.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Bool
XRegisterIMInstantiateCallback(
    TCL_UNUSED(Display *),
    TCL_UNUSED(struct _XrmHashBucketRec *),
    TCL_UNUSED(char *),
    TCL_UNUSED(char *),
    TCL_UNUSED(XIDProc),
    TCL_UNUSED(XPointer))
{
    /* No-op - input methods not used in Wayland port. */
    return False;
}

/*
 *----------------------------------------------------------------------
 *
 * XmbLookupString --
 *
 *	Look up composed string from input method. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0, with status set to XLookupNone if non-NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XmbLookupString(
    TCL_UNUSED(XIC),
    XKeyPressedEvent *event,  /* Note: XKeyPressedEvent*, not char* */
    TCL_UNUSED(char *),
    TCL_UNUSED(int),
    TCL_UNUSED(KeySym *),
    int *status)
{
    /* No-op - input method composition not used in Wayland port. */
    (void)event;  /* Suppress unused parameter warning */
    if (status) {
        *status = XLookupNone;
    }
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XLookupColor --
 *
 *	Look up color by name. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (False).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Status
XLookupColor(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Colormap),
    const char *spec,  /* Note: const char*, not char* */
    TCL_UNUSED(XColor *),
    TCL_UNUSED(XColor *))
{
    /* No-op - color lookup handled by NanoVG. */
    (void)spec;  /* Suppress unused parameter warning */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XKeysymToKeycode --
 *
 *	Convert keysym to keycode. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

KeyCode
XKeysymToKeycode(
    TCL_UNUSED(Display *),
    TCL_UNUSED(KeySym))
{
    /* No-op - keyboard mapping handled by GLFW. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XDestroyRegion --
 *
 *	Destroy a region. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XDestroyRegion(
    TCL_UNUSED(Region))
{
    /* No-op - regions not used in Wayland port. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XSelectInput --
 *
 *	Select input event mask. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XSelectInput(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(long))
{
    /* No-op - event selection handled by GLFW/Tk. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XCreateColormap --
 *
 *	Create a colormap. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (None).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Colormap
XCreateColormap(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(Visual *),
    TCL_UNUSED(int))
{
    /* No-op - colormaps not used in Wayland port. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XSetClipMask --
 *
 *	Set clip mask in GC. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XSetClipMask(
    TCL_UNUSED(Display *),
    TCL_UNUSED(GC),
    TCL_UNUSED(Pixmap))
{
    /* No-op - clipping handled by NanoVG. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XBell --
 *
 *	Ring the bell. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XBell(
    TCL_UNUSED(Display *),
    TCL_UNUSED(int))
{
    /* No-op - audible bell not supported in Wayland port. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XUnregisterIMInstantiateCallback --
 *
 *	Unregister input method instantiation callback. No-op in Wayland port.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Bool
XUnregisterIMInstantiateCallback(
    TCL_UNUSED(Display *),
    TCL_UNUSED(struct _XrmHashBucketRec *),
    TCL_UNUSED(char *),
    TCL_UNUSED(char *),
    TCL_UNUSED(XIDProc),
    TCL_UNUSED(XPointer))
{
    /* No-op - input methods not used in Wayland port. */
    return False;
}

/*
 *----------------------------------------------------------------------
 *
 * XCreatePixmapCursor --
 *
 *	Create a cursor from pixmaps. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (None).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Cursor
XCreatePixmapCursor(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Pixmap),
    TCL_UNUSED(Pixmap),
    TCL_UNUSED(XColor *),
    TCL_UNUSED(XColor *),
    TCL_UNUSED(unsigned int),
    TCL_UNUSED(unsigned int))
{
    /* No-op - cursor creation handled by GLFW. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XParseColor --
 *
 *	Parse color string. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (False).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Status
XParseColor(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Colormap),
    const char *spec,  /* Note: const char*, not char* */
    TCL_UNUSED(XColor *))
{
    /* No-op - color parsing handled by NanoVG/Tk. */
    (void)spec;  /* Suppress unused parameter warning */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XGetWMColormapWindows --
 *
 *	Get colormap windows property. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Status
XGetWMColormapWindows(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    Window **windows_return,
    int *count_return)  /* Note: int*, not int** */
{
    /* No-op - colormaps not used in Wayland port. */
    if (windows_return) {
        *windows_return = NULL;
    }
    if (count_return) {
        *count_return = 0;
    }
    return 0;
}
/*
 *----------------------------------------------------------------------
 *
 * XWithdrawWindow --
 *
 *	Withdraw a window. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XWithdrawWindow(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(int))
{
    /* No-op - window withdrawal handled by GLFW/Tk. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XGetVisualInfo --
 *
 *	Get visual information. No-op in Wayland port.
 *
 * Results:
 *	Always returns NULL, with nitems_return set to 0 if non-NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

XVisualInfo*
XGetVisualInfo(
    TCL_UNUSED(Display *),
    TCL_UNUSED(long),
    TCL_UNUSED(XVisualInfo *),
    int *nitems_return)
{
    /* No-op - visuals not used in Wayland port. */
    if (nitems_return) {
        *nitems_return = 0;
    }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * XAllocColor --
 *
 *	Allocate a color. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (False).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XAllocColor(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Colormap),
    TCL_UNUSED(XColor *))
{
    /* No-op - color allocation handled by NanoVG. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XChangeProperty --
 *
 *	Change a window property. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XChangeProperty(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(Atom),
    TCL_UNUSED(Atom),
    TCL_UNUSED(int),
    TCL_UNUSED(int),
    const unsigned char *data,  /* Note: const unsigned char*, not unsigned char* */
    TCL_UNUSED(int))
{
    /* No-op - X properties not used in Wayland port. */
    (void)data;  /* Suppress unused parameter warning */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XCreateGlyphCursor --
 *
 *	Create a glyph cursor. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (None).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Cursor
XCreateGlyphCursor(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Font),
    TCL_UNUSED(Font),
    TCL_UNUSED(unsigned int),
    TCL_UNUSED(unsigned int),
    const XColor *fc,  /* Note: const XColor*, not XColor* */
    const XColor *bc)  /* Note: const XColor*, not XColor* */
{
    /* No-op - cursor creation handled by GLFW. */
    (void)fc;  /* Suppress unused parameter warning */
    (void)bc;  /* Suppress unused parameter warning */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XSetClipOrigin --
 *
 *	Set clip origin in GC. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XSetClipOrigin(
    TCL_UNUSED(Display *),
    TCL_UNUSED(GC),
    TCL_UNUSED(int),
    TCL_UNUSED(int))
{
    /* No-op - clipping handled by NanoVG. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XSetIconName --
 *
 *	Set window icon name. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XSetIconName(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    const char *name)  /* Note: const char*, not char* */
{
    /* No-op - icon names not exposed in Wayland. */
    (void)name;  /* Suppress unused parameter warning */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XLookupString --
 *
 *	Look up string from key event. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XLookupString(
    TCL_UNUSED(XKeyEvent *),
    TCL_UNUSED(char *),
    TCL_UNUSED(int),
    TCL_UNUSED(KeySym *),
    TCL_UNUSED(XComposeStatus *))
{
    /* No-op - keyboard input handled by GLFW. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XCreateIC --
 *
 *	Create input context. No-op in Wayland port.
 *
 * Results:
 *	Always returns NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

XIC
XCreateIC(
    TCL_UNUSED(XIM),
    ...)
{
    /* No-op - input contexts not used in Wayland port. */
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * XDestroyIC --
 *
 *	Destroy input context. No-op in Wayland port.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
XDestroyIC(
    TCL_UNUSED(XIC))
{
    /* No-op - input contexts not used in Wayland port. */
    return;
}

/*
 *----------------------------------------------------------------------
 *
 * XUngrabPointer --
 *
 *	Ungrab the pointer. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XUngrabPointer(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Time))
{
    /* No-op - pointer grabbing not supported in Wayland. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XClearWindow --
 *
 *	Clear a window. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XClearWindow(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window))
{
    /* No-op - window clearing handled by NanoVG redraw. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XkbOpenDisplay --
 *
 *	Open display with XKB support. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (None).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Display*
XkbOpenDisplay(
    const char *name,  /* Note: const char*, not char* */
    TCL_UNUSED(int *),
    TCL_UNUSED(int *),
    TCL_UNUSED(int *),
    TCL_UNUSED(int *),
    TCL_UNUSED(int *))
{
    /* No-op - XKB not used in Wayland port. */
    (void)name;  /* Suppress unused parameter warning */
    return XOpenDisplay(NULL);
}

/*
 *----------------------------------------------------------------------
 *
 * XSetTSOrigin --
 *
 *	Set tile/stipple origin in GC. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XSetTSOrigin(
    TCL_UNUSED(Display *),
    TCL_UNUSED(GC),
    TCL_UNUSED(int),
    TCL_UNUSED(int))
{
    /* No-op - tiling/stippling handled by NanoVG. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XGrabServer --
 *
 *	Grab the X server. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XGrabServer(
    TCL_UNUSED(Display *))
{
    /* No-op - server grabbing not applicable to Wayland. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XCreateRegion --
 *
 *	Create a region. No-op in Wayland port.
 *
 * Results:
 *	Always returns NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Region
XCreateRegion(
    void)
{
    /* No-op - regions not used in Wayland port. */
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * XStringListToTextProperty --
 *
 *	Convert string list to text property. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (False).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XStringListToTextProperty(
    TCL_UNUSED(char **),
    TCL_UNUSED(int),
    TCL_UNUSED(XTextProperty *))
{
    /* No-op - text properties not used in Wayland port. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XCloseIM --
 *
 *	Close input method. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XCloseIM(
    TCL_UNUSED(XIM))
{
    /* No-op - input methods not used in Wayland port. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XXorRegion --
 *
 *	XOR two regions. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XXorRegion(
    TCL_UNUSED(Region),
    TCL_UNUSED(Region),
    TCL_UNUSED(Region))
{
    /* No-op - regions not used in Wayland port. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XPutBackEvent --
 *
 *	Put an event back into the queue. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XPutBackEvent(
    TCL_UNUSED(Display *),
    TCL_UNUSED(XEvent *))
{
    /* No-op - event queue managed by GLFW. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XSetWMClientMachine --
 *
 *	Set WM_CLIENT_MACHINE property. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
XSetWMClientMachine(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(XTextProperty *))
{
    /* No-op - ICCCM properties not used in Wayland port. */
    return;
}

/*
 *----------------------------------------------------------------------
 *
 * XFreeCursor --
 *
 *	Free a cursor. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XFreeCursor(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Cursor))
{
    /* No-op - cursor destruction handled by GLFW. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XIntersectRegion --
 *
 *	Intersect two regions. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XIntersectRegion(
    TCL_UNUSED(Region),
    TCL_UNUSED(Region),
    TCL_UNUSED(Region))
{
    /* No-op - regions not used in Wayland port. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XEqualRegion --
 *
 *	Test region equality. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (False).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XEqualRegion(
    TCL_UNUSED(Region),
    TCL_UNUSED(Region))
{
    /* No-op - regions not used in Wayland port. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XCreateBitmapFromData --
 *
 *	Create a bitmap from data. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Pixmap
XCreateBitmapFromData(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Drawable),
    const char *data,  /* Note: const char*, not char* */
    TCL_UNUSED(unsigned int),
    TCL_UNUSED(unsigned int))
{
    /* No-op - bitmaps not used in Wayland port. */
    (void)data;  /* Suppress unused parameter warning */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XFreeModifiermap --
 *
 *	Free a modifier map. No-op in Wayland port.
 *
 * Results:
 *	Always returns NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XFreeModifiermap(
    TCL_UNUSED(XModifierKeymap *))
{
    /* No-op - modifier maps not used in Wayland port. */
    return 0;
}
/*
 *----------------------------------------------------------------------
 *
 * XSubtractRegion --
 *
 *	Subtract one region from another. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XSubtractRegion(
    TCL_UNUSED(Region),
    TCL_UNUSED(Region),
    TCL_UNUSED(Region))
{
    /* No-op - regions not used in Wayland port. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XPolygonRegion --
 *
 *	Create region from polygon. No-op in Wayland port.
 *
 * Results:
 *	Always returns NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Region
XPolygonRegion(
    TCL_UNUSED(XPoint *),
    TCL_UNUSED(int),
    TCL_UNUSED(int))
{
    /* No-op - regions not used in Wayland port. */
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * XCreateFontSet --
 *
 *	Create a font set. No-op in Wayland port.
 *
 * Results:
 *	Always returns NULL, with missing_list/count set appropriately.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

XFontSet
XCreateFontSet(
    TCL_UNUSED(Display *),
    const char *base,  /* Note: const char*, not char* */
    char ***missing_list,
    int *missing_count,
    TCL_UNUSED(char **))
{
    /* No-op - font sets not used in Wayland port. */
    (void)base;  /* Suppress unused parameter warning */
    if (missing_list) {
        *missing_list = NULL;
    }
    if (missing_count) {
        *missing_count = 0;
    }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * XRefreshKeyboardMapping --
 *
 *	Refresh keyboard mapping. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XRefreshKeyboardMapping(
    TCL_UNUSED(XMappingEvent *))
{
    /* No-op - keyboard mapping handled by GLFW. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XGrabKeyboard --
 *
 *	Grab the keyboard. No-op in Wayland port.
 *
 * Results:
 *	Always returns GrabSuccess.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XGrabKeyboard(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(Bool),
    TCL_UNUSED(int),
    TCL_UNUSED(int),
    TCL_UNUSED(Time))
{
    /* No-op - keyboard grabbing not supported in Wayland. */
    return GrabSuccess;
}

/*
 *----------------------------------------------------------------------
 *
 * XCreateImage --
 *
 *	Create an X image. No-op in Wayland port.
 *
 * Results:
 *	Always returns NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

XImage*
XCreateImage(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Visual *),
    TCL_UNUSED(unsigned int),
    TCL_UNUSED(int),
    TCL_UNUSED(int),
    TCL_UNUSED(char *),
    TCL_UNUSED(unsigned int),
    TCL_UNUSED(unsigned int),
    TCL_UNUSED(int),
    TCL_UNUSED(int))
{
    /* No-op - X images not used in Wayland port. */
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * XUnionRegion --
 *
 *	Union two regions. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XUnionRegion(
    TCL_UNUSED(Region),
    TCL_UNUSED(Region),
    TCL_UNUSED(Region))
{
    /* No-op - regions not used in Wayland port. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XFreeColors --
 *
 *	Free allocated colors. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XFreeColors(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Colormap),
    TCL_UNUSED(unsigned long *),
    TCL_UNUSED(int),
    TCL_UNUSED(unsigned long))
{
    /* No-op - color allocation handled by NanoVG. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XUngrabKeyboard --
 *
 *	Ungrab the keyboard. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XUngrabKeyboard(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Time))
{
    /* No-op - keyboard grabbing not supported in Wayland. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XListHosts --
 *
 *	List hosts. No-op in Wayland port.
 *
 * Results:
 *	Always returns NULL, with nhosts set to 0 if non-NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

XHostAddress*
XListHosts(
    TCL_UNUSED(Display *),
    int *nhosts,
    TCL_UNUSED(Bool *))
{
    /* No-op - host access control not used in Wayland port. */
    if (nhosts) {
        *nhosts = 0;
    }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * XSetLocaleModifiers --
 *
 *	Set locale modifiers. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (False).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

char*
XSetLocaleModifiers(
    const char *modifiers)  /* Note: const char*, not char* */
{
    /* No-op - locale handling via standard C locale. */
    (void)modifiers;  /* Suppress unused parameter warning */
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * XFreeStringList --
 *
 *	Free a string list. No-op in Wayland port.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
XFreeStringList(
    TCL_UNUSED(char **))
{
    /* No-op - string lists not used in Wayland port. */
    return;
}

/*
 *----------------------------------------------------------------------
 *
 * XSetDashes --
 *
 *	Set line dashes in GC. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XSetDashes(
    TCL_UNUSED(Display *),
    TCL_UNUSED(GC),
    TCL_UNUSED(int),
    const char *dash_list,  /* Note: const char*, not char* */
    TCL_UNUSED(int))
{
    /* No-op - line dashing handled by NanoVG. */
    (void)dash_list;  /* Suppress unused parameter warning */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XNextEvent --
 *
 *	Get next event. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XNextEvent(
    TCL_UNUSED(Display *),
    TCL_UNUSED(XEvent *))
{
    /* No-op - event handling via GLFW. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XGetGeometry --
 *
 *	Get window geometry. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (False).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XGetGeometry(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Drawable),
    TCL_UNUSED(Window *),
    TCL_UNUSED(int *),
    TCL_UNUSED(int *),
    TCL_UNUSED(unsigned int *),
    TCL_UNUSED(unsigned int *),
    TCL_UNUSED(unsigned int *),
    TCL_UNUSED(unsigned int *))
{
    /* No-op - geometry tracked by Tk/GLFW. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XWarpPointer --
 *
 *	Move pointer. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XWarpPointer(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(Window),
    TCL_UNUSED(int),
    TCL_UNUSED(int),
    TCL_UNUSED(unsigned int),
    TCL_UNUSED(unsigned int),
    TCL_UNUSED(int),
    TCL_UNUSED(int))
{
    /* No-op - pointer warping not supported in Wayland. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XSetStipple --
 *
 *	Set stipple pattern in GC. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XSetStipple(
    TCL_UNUSED(Display *),
    TCL_UNUSED(GC),
    TCL_UNUSED(Pixmap))
{
    /* No-op - stippling handled by NanoVG. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XSetRegion --
 *
 *	Set clip region in GC. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XSetRegion(
    TCL_UNUSED(Display *),
    TCL_UNUSED(GC),
    TCL_UNUSED(Region))
{
    /* No-op - clipping handled by NanoVG. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XFlush --
 *
 *	Flush output buffer. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XFlush(
    TCL_UNUSED(Display *))
{
    /* No-op - GLFW handles flushing. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XGetModifierMapping --
 *
 *	Get modifier mapping. No-op in Wayland port.
 *
 * Results:
 *	Always returns NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

XModifierKeymap*
XGetModifierMapping(
    TCL_UNUSED(Display *))
{
    /* No-op - modifier mapping handled by GLFW. */
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * XOffsetRegion --
 *
 *	Offset a region. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XOffsetRegion(
    TCL_UNUSED(Region),
    TCL_UNUSED(int),
    TCL_UNUSED(int))
{
    /* No-op - regions not used in Wayland port. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XSetICValues --
 *
 *	Set input context values. No-op in Wayland port.
 *
 * Results:
 *	Always returns NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

char*
XSetICValues(
    TCL_UNUSED(XIC),
    ...)
{
    /* No-op - input contexts not used in Wayland port. */
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * XSetWindowColormap --
 *
 *	Set window colormap. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XSetWindowColormap(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(Colormap))
{
    /* No-op - colormaps not used in Wayland port. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XRootWindow --
 *
 *	Get root window. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (None).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Window
XRootWindow(
    TCL_UNUSED(Display *),
    TCL_UNUSED(int))
{
    /* No-op - root window concept not applicable to Wayland. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XDrawLine --
 *
 *	Draw a line. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XDrawLine(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Drawable),
    TCL_UNUSED(GC),
    TCL_UNUSED(int),
    TCL_UNUSED(int),
    TCL_UNUSED(int),
    TCL_UNUSED(int))
{
    /* No-op - drawing handled by NanoVG. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XTranslateCoordinates --
 *
 *	Translate coordinates between windows. No-op in Wayland port.
 *
 * Results:
 *	Always returns False.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Bool
XTranslateCoordinates(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(Window),
    TCL_UNUSED(int),
    TCL_UNUSED(int),
    TCL_UNUSED(int *),
    TCL_UNUSED(int *),
    TCL_UNUSED(Window *))
{
    /* No-op - coordinate translation not needed in Wayland. */
    return False;
}

/*
 *----------------------------------------------------------------------
 *
 * XDeleteProperty --
 *
 *	Delete a window property. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XDeleteProperty(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(Atom))
{
    /* No-op - X properties not used in Wayland port. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XVisualIDFromVisual --
 *
 *	Get visual ID from visual. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

VisualID
XVisualIDFromVisual(
    TCL_UNUSED(Visual *))
{
    /* No-op - visuals not used in Wayland port. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XRectInRegion --
 *
 *	Test if rectangle is in region. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (RectangleOut).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XRectInRegion(
    TCL_UNUSED(Region),
    TCL_UNUSED(int),
    TCL_UNUSED(int),
    TCL_UNUSED(unsigned int),
    TCL_UNUSED(unsigned int))
{
    /* No-op - regions not used in Wayland port. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XSetICFocus --
 *
 *	Set input context focus. No-op in Wayland port.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
XSetICFocus(
    TCL_UNUSED(XIC))
{
    /* No-op - input contexts not used in Wayland port. */
    return;
}

/*
 *----------------------------------------------------------------------
 *
 * XSetCommand --
 *
 *	Set WM_COMMAND property. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XSetCommand(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(char **),
    TCL_UNUSED(int))
{
    /* No-op - ICCCM properties not used in Wayland port. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XGetICValues --
 *
 *	Get input context values. No-op in Wayland port.
 *
 * Results:
 *	Always returns NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

char*
XGetICValues(
    TCL_UNUSED(XIC),
    ...)
{
    /* No-op - input contexts not used in Wayland port. */
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * XOpenIM --
 *
 *	Open input method. No-op in Wayland port.
 *
 * Results:
 *	Always returns NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

XIM
XOpenIM(
    TCL_UNUSED(Display *),
    TCL_UNUSED(struct _XrmHashBucketRec *),
    TCL_UNUSED(char *),
    TCL_UNUSED(char *))
{
    /* No-op - input methods not used in Wayland port. */
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * XGetInputFocus --
 *
 *	Get current input focus. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XGetInputFocus(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window *),
    TCL_UNUSED(int *))
{
    /* No-op - input focus tracked by GLFW. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XDefineCursor --
 *
 *	Define window cursor. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XDefineCursor(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(Cursor))
{
    /* No-op - cursor handling via GLFW. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XSetFunction --
 *
 *	Set GC function. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XSetFunction(
    TCL_UNUSED(Display *),
    TCL_UNUSED(GC),
    TCL_UNUSED(int))
{
    /* No-op - drawing functions handled by NanoVG. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XVaCreateNestedList --
 *
 *	Create a nested variable argument list. No-op in Wayland port.
 *
 * Results:
 *	Always returns NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

XVaNestedList
XVaCreateNestedList(
    TCL_UNUSED(int),
    ...)
{
    /* No-op - nested lists not used in Wayland port. */
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * XSetClipRectangles --
 *
 *	Set clip rectangles in GC. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XSetClipRectangles(
    TCL_UNUSED(Display *),
    TCL_UNUSED(GC),
    TCL_UNUSED(int),
    TCL_UNUSED(int),
    TCL_UNUSED(XRectangle *),
    TCL_UNUSED(int),
    TCL_UNUSED(int))
{
    /* No-op - clipping handled by NanoVG. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XGetWindowAttributes --
 *
 *	Get window attributes. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (False).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XGetWindowAttributes(
    TCL_UNUSED(Display *),
    TCL_UNUSED(Window),
    TCL_UNUSED(XWindowAttributes *))
{
    /* No-op - window attributes tracked by Tk. */
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * XUnionRectWithRegion --
 *
 *	Union rectangle with region. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XUnionRectWithRegion(
    TCL_UNUSED(XRectangle *),
    TCL_UNUSED(Region),
    TCL_UNUSED(Region))
{
    /* No-op - regions not used in Wayland port. */
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * XSetArcMode --
 *
 *	Set arc mode in GC. No-op in Wayland port.
 *
 * Results:
 *	Always returns 0 (Success).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
XSetArcMode(
    TCL_UNUSED(Display *),
    TCL_UNUSED(GC),
    TCL_UNUSED(int))
{
    /* No-op - arc drawing handled by NanoVG. */
    return 0;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
