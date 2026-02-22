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

/*
 * The Xlib Display type is intentionally opaque (incomplete struct) in
 * modern Xlib headers, so we cannot sizeof() it or access its members.
 * We define our own private layout that holds exactly what this port needs,
 * and cast to/from Display * only at API boundaries.
 */
typedef struct TkWaylandDisplay_ {
    Screen    *screens;
    int        nscreens;
    int        default_screen;
    char      *display_name;
} TkWaylandDisplay;

/*
 * DefaultScreenOfDisplay, DefaultScreen, DefaultVisual, DefaultColormap,
 * and DefaultDepth are macros in <X11/Xlib.h>.  Undefine them so we can
 * provide real function implementations below.
 */
#undef DefaultScreenOfDisplay
#undef DefaultScreen
#undef DefaultVisual
#undef DefaultColormap
#undef DefaultDepth


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
 * Display / Screen / Atom Stubs
 *
 *	These stubs are consolidated here so that every Xlib
 *	compatibility symbol resides in the emulation layer.
 *
 *======================================================================
 */

/*
 *----------------------------------------------------------------------
 *
 * DefaultVisual --
 *
 *	Return the default visual for a display.
 *
 * Results:
 *	Pointer to the default Visual structure.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Visual *
DefaultVisual(
    Display *display,
    TCL_UNUSED(int))
{
    TkWaylandDisplay *wd = (TkWaylandDisplay *)display;
    if (wd == NULL || wd->screens == NULL) {
        return NULL;
    }
    return wd->screens[0].root_visual;
}


/*
 *----------------------------------------------------------------------
 *
 * DefaultColormap --
 *
 *	Return the default colormap.
 *
 * Results:
 *	Colormap handle (synthesized value 1).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */


Colormap
DefaultColormap(
    TCL_UNUSED(Display *),
    TCL_UNUSED(int))
{
    return (Colormap)1;
}

/*
 *----------------------------------------------------------------------
 *
 * DefaultDepth --
 *
 *	Return the default depth for a display.
 *
 * Results:
 *	Root depth (always 24).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
DefaultDepth(
    Display *display,
    TCL_UNUSED(int))
{
    TkWaylandDisplay *wd = (TkWaylandDisplay *)display;
    if (wd == NULL || wd->screens == NULL) {
        return 0;
    }
    return wd->screens[0].root_depth;
}

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

int
TkUnixDoOneXEvent(
    TCL_UNUSED(Tcl_Time *))  /* timePtr */
{
    /* No-op. */
    return 0;
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
 * TkScrollWindow --
 *
 *	Scroll a window.  Not implemented in Wayland port.
 *
 * Results:
 *	Always returns 0 (False).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

bool
TkScrollWindow(
    TCL_UNUSED(Tk_Window),
    TCL_UNUSED(GC),
    TCL_UNUSED(int),
    TCL_UNUSED(int),
    TCL_UNUSED(int),
    TCL_UNUSED(int),
    TCL_UNUSED(int),
    TCL_UNUSED(int),
    TCL_UNUSED(TkRegion))
{
    return 0;
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


void
TkpSync(
    TCL_UNUSED(Display *))	/* Display to sync. */
{
    /* No-op */
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
