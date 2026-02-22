/*
 * tkGlfwInt.h --
 *
 *	This file contains declarations that are shared among the
 *	GLFW/Wayland-specific parts of Tk.
 *
 * Copyright © 1995-1997 Sun Microsystems, Inc.
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef _TKGLFWINT_H
#define _TKGLFWINT_H

#include "tkInt.h"
#include "tkUnixInt.h"
#include <GLFW/glfw3.h>
#include <GLES3/gl3.h>
#include "nanovg.h"
#include "tkIntPlatDecls.h"
#include "tkWaylandDefaults.h"

/*
 *----------------------------------------------------------------------
 *
 * Core Context Structure
 *
 *----------------------------------------------------------------------
 */

typedef struct {
    GLFWwindow *mainWindow;     /* Shared context window */
    NVGcontext *vg;             /* Global NanoVG context */
    int         initialized;    /* Initialization flag */
} TkGlfwContext;

/*
 *----------------------------------------------------------------------
 *
 * Window Mapping Structure
 *
 *	Maintains the bidirectional mapping between Tk windows,
 *	GLFW windows, and Drawables.
 *
 *----------------------------------------------------------------------
 */

typedef struct WindowMapping {
    TkWindow           *tkWindow;   /* Tk window pointer */
    GLFWwindow         *glfwWindow; /* Corresponding GLFW window */
    Drawable            drawable;   /* X11-style drawable ID */
    int                 width;      /* Current width */
    int                 height;     /* Current height */
    struct WindowMapping *nextPtr;  /* Next in linked list */
} WindowMapping;

/*
 * ---------------------------------------------------------------------
 * 
 * Private Display subtype for the Wayland backend.  The embedded Display
 * must be the first member so that a TkWaylandDisplay * can be safely
 * cast to Display * and back at API boundaries.
 * 
 * ---------------------------------------------------------------------
 */
typedef struct TkWaylandDisplay_ {
    Display   *display;      
    Screen    *screens;
    int        nscreens;
    int        default_screen;
    char      *display_name;
} TkWaylandDisplay;

/* Accessor functions for the Display struct. */
MODULE_SCOPE TkWaylandDisplay *TkWaylandGetWd(void);
MODULE_SCOPE TkDisplay        *TkWaylandGetDispPtr(void);

/*
 *----------------------------------------------------------------------
 *
 * ProtocolHandler – per-protocol Tcl command binding.
 *
 *----------------------------------------------------------------------
 */

typedef struct ProtocolHandler {
    int                    protocol;  /* Protocol identifier. */
    struct ProtocolHandler *nextPtr;
    Tcl_Interp            *interp;
    char                   command[TKFLEXARRAY];
} ProtocolHandler;

#define HANDLER_SIZE(cmdLength) \
    (offsetof(ProtocolHandler, command) + 1 + (cmdLength))
    
/*
 *----------------------------------------------------------------------
 *
 * WmAttributes – per-window wm attribute state.
 *
 *----------------------------------------------------------------------
 */

typedef struct {
    double alpha;       /* 0.0 = transparent, 1.0 = opaque */
    int    topmost;
    int    zoomed;
    int    fullscreen;
} WmAttributes;

typedef enum {
    WMATT_ALPHA,
    WMATT_FULLSCREEN,
    WMATT_TOPMOST,
    WMATT_TYPE,
    WMATT_ZOOMED,
    _WMATT_LAST_ATTRIBUTE
} WmAttribute;

/* Defined in tkWaylandWm.c. */
extern const char *const WmAttributeNames[];

/*
 *----------------------------------------------------------------------
 *
 * TkWmInfo – per-toplevel window manager state.
 *
 *----------------------------------------------------------------------
 */

typedef struct TkWmInfo {
    TkWindow    *winPtr;        /* Tk window. */
    GLFWwindow  *glfwWindow;    /* GLFW handle (NULL until first map). */
    char        *title;
    char        *iconName;
    char        *leaderName;
    TkWindow    *containerPtr;  /* Transient-for container. */
    Tk_Window    icon;
    Tk_Window    iconFor;
    int          withdrawn;

    /* Wrapper / menubar. */
    TkWindow    *wrapperPtr;
    Tk_Window    menubar;
    int          menuHeight;

    /* Size hints. */
    int          sizeHintsFlags;
    int          minWidth, minHeight;
    int          maxWidth, maxHeight;
    Tk_Window    gridWin;
    int          widthInc, heightInc;
    struct { int x; int y; } minAspect, maxAspect;
    int          reqGridWidth, reqGridHeight;
    int          gravity;

    /* Position / size. */
    int          width, height;
    int          x, y;
    int          parentWidth, parentHeight;
    int          xInParent, yInParent;
    int          configWidth, configHeight;

    /* Virtual root (compatibility). */
    int          vRootX, vRootY;
    int          vRootWidth, vRootHeight;

    /* Misc. */
    WmAttributes attributes;
    WmAttributes reqState;
    ProtocolHandler *protPtr;
    Tcl_Size     cmdArgc;
    Tcl_Obj    **cmdArgv;
    char        *clientMachine;
    int          flags;
    int          numTransients;
    int          iconDataSize;
    unsigned char *iconDataPtr;
    GLFWimage   *glfwIcon;
    int          glfwIconCount;
    int          isMapped;
    int          lastX, lastY;
    int          lastWidth, lastHeight;

    struct TkWmInfo *nextPtr;
} WmInfo;


/*
 *----------------------------------------------------------------------
 *
 * Drawing Context Structure
 *
 *	Temporary structure used during drawing operations to maintain
 *	state and ensure proper cleanup.
 *
 *----------------------------------------------------------------------
 */

typedef struct {
    NVGcontext *vg;         /* NanoVG context for this draw */
    Drawable    drawable;   /* Target drawable */
    GLFWwindow *glfwWindow; /* Associated GLFW window */
    int         width;      /* Drawable width */
    int         height;     /* Drawable height */
} TkWaylandDrawingContext;

/*
 *----------------------------------------------------------------------
 *
 * Minimal Graphics Context Structure
 *
 *	Internal GC used by all drawing operations. 
 *
 *----------------------------------------------------------------------
 */

typedef struct TkWaylandGCStruct {
    unsigned long foreground; /* Foreground color (pixel value) */
    unsigned long background; /* Background color (pixel value) */
    int           line_width; /* Line width in pixels */
    int           line_style; /* LineSolid, LineOnOffDash, etc. */
    int           cap_style;  /* CapButt, CapRound, CapProjecting */
    int           join_style; /* JoinMiter, JoinRound, JoinBevel */
    int           fill_rule;  /* EvenOddRule or WindingRule */
    int           arc_mode;   /* ArcChord or ArcPieSlice */
    void         *font;       /* Font handle (reserved) */
} TkWaylandGCImpl;

/*
 *----------------------------------------------------------------------
 *
 * Pixmap Structure
 *
 *	Internal pixmap used by all pixmap/image operations.
 *
 *----------------------------------------------------------------------
 */

typedef struct TkWaylandPixmapStruct {
    int      imageId; /* NanoVG image ID (type 0) */
    NVGpaint paint;   /* NanoVG paint (type 1, fallback) */
    int      width;
    int      height;
    int      depth;
    int      type;    /* 0 = image, 1 = paint */
} TkWaylandPixmapImpl;

/*
 *----------------------------------------------------------------------
 *
 * GC value-mask constants (mirror X11 values for compatibility)
 *
 *----------------------------------------------------------------------
 */

#ifndef GCForeground
#define GCForeground    (1L<<2)
#endif
#ifndef GCBackground
#define GCBackground    (1L<<3)
#endif
#ifndef GCLineWidth
#define GCLineWidth     (1L<<4)
#endif
#ifndef GCLineStyle
#define GCLineStyle     (1L<<5)
#endif
#ifndef GCCapStyle
#define GCCapStyle      (1L<<6)
#endif
#ifndef GCJoinStyle
#define GCJoinStyle     (1L<<7)
#endif
#ifndef GCFillRule
#define GCFillRule      (1L<<9)
#endif
#ifndef GCArcMode
#define GCArcMode       (1L<<22)
#endif
#ifndef GCFont
#define GCFont          (1L<<14)
#endif

/*
 *----------------------------------------------------------------------
 *
 * Global State Access
 *
 *----------------------------------------------------------------------
 */

/* Get the global GLFW/NanoVG context. */
MODULE_SCOPE TkGlfwContext *TkGlfwGetContext(void);

/*
 *----------------------------------------------------------------------
 *
 * Initialization and Cleanup
 *
 *----------------------------------------------------------------------
 */

/* Initialize GLFW and NanoVG. */
MODULE_SCOPE int  TkGlfwInitialize(void);

/* Clean up all GLFW/NanoVG resources. */
MODULE_SCOPE void TkGlfwCleanup(void);

/*
 *----------------------------------------------------------------------
 *
 * Window Management
 *
 *----------------------------------------------------------------------
 */

/* Create a new GLFW window and register mapping. */
MODULE_SCOPE GLFWwindow *TkGlfwCreateWindow(
    TkWindow   *tkWin,
    int         width,
    int         height,
    const char *title,
    Drawable   *drawableOut);

/* Destroy a GLFW window and remove mapping. */
MODULE_SCOPE void TkGlfwDestroyWindow(GLFWwindow *glfwWindow);

/* Get GLFW window from Tk window. */
MODULE_SCOPE GLFWwindow *TkGlfwGetGLFWWindow(Tk_Window tkwin);

/* Get Tk window from GLFW window. */
MODULE_SCOPE TkWindow   *TkGlfwGetTkWindow(GLFWwindow *glfwWindow);

/* Get GLFW window from Drawable ID. */
MODULE_SCOPE GLFWwindow *TkGlfwGetWindowFromDrawable(Drawable drawable);

/* Update window size in mapping. */
MODULE_SCOPE void TkGlfwUpdateWindowSize(
    GLFWwindow *glfwWindow,
    int         width,
    int         height);

/*
 *----------------------------------------------------------------------
 *
 * Drawing Context Management
 *
 *----------------------------------------------------------------------
 */

/* Set up drawing context for a drawable. */
MODULE_SCOPE int  TkGlfwBeginDraw(
    Drawable                drawable,
    GC                      gc,
    TkWaylandDrawingContext *dcPtr);

/* Clean up and present drawing context. */
MODULE_SCOPE void TkGlfwEndDraw(TkWaylandDrawingContext *dcPtr);

/* Get NanoVG context for current drawing. */
MODULE_SCOPE NVGcontext *TkGlfwGetNVGContext(void);

/*
 *----------------------------------------------------------------------
 *
 * GC Internals
 *
 *	These entry points provide low-level access to the GC structure
 *	and are used by both the Xlib emulation layer and the drawing
 *	code. All external code must go through these rather than casting
 *	a GC pointer directly.
 *
 *----------------------------------------------------------------------
 */

/* Allocate and initialise a new GC, optionally applying valuemask/values. */
MODULE_SCOPE GC   TkWaylandCreateGC(
    unsigned long  valuemask,
    XGCValues     *values);

/* Free a GC allocated by TkWaylandCreateGC. */
MODULE_SCOPE void TkWaylandFreeGC(GC gc);

/* Read fields out of a GC according to valuemask. */
MODULE_SCOPE int  TkWaylandGetGCValues(
    GC             gc,
    unsigned long  valuemask,
    XGCValues     *values);

/* Write fields into a GC according to valuemask. */
MODULE_SCOPE int  TkWaylandChangeGC(
    GC             gc,
    unsigned long  valuemask,
    XGCValues     *values);

/* Copy fields from src GC to dst GC according to valuemask. */
MODULE_SCOPE int  TkWaylandCopyGC(
    GC            src,
    unsigned long valuemask,
    GC            dst);

/*
 *----------------------------------------------------------------------
 *
 * Pixmap Internals
 *
 *----------------------------------------------------------------------
 */

/* Create a pixmap (image or paint depending on size/context). */
MODULE_SCOPE Pixmap TkWaylandCreatePixmap(
    int width,
    int height,
    int depth);

/* Free a pixmap. */
MODULE_SCOPE void TkWaylandFreePixmap(Pixmap pixmap);

/* Helpers to inspect a pixmap. */
MODULE_SCOPE int       TkWaylandGetPixmapImageId(Pixmap pixmap);
MODULE_SCOPE NVGpaint *TkWaylandGetPixmapPaint(Pixmap pixmap);
MODULE_SCOPE int       TkWaylandGetPixmapType(Pixmap pixmap);
MODULE_SCOPE void      TkWaylandGetPixmapDimensions(
    Pixmap pixmap,
    int   *width,
    int   *height,
    int   *depth);

/* Replace a pixmap's image data (type-0 only). */
MODULE_SCOPE int TkWaylandUpdatePixmapImage(
    Pixmap              pixmap,
    const unsigned char *data);

/* Release all pixmap resources at shutdown. */
MODULE_SCOPE void TkWaylandCleanupPixmapStore(void);

/* Set / get the NanoVG context used for pixmap operations. */
MODULE_SCOPE void       TkWaylandSetNVGContext(NVGcontext *vg);
MODULE_SCOPE NVGcontext *TkWaylandGetPixmapNVGContext(void);

/*
 *----------------------------------------------------------------------
 *
 * Event Processing
 *
 *----------------------------------------------------------------------
 */

/* Process pending GLFW events. */
MODULE_SCOPE void TkGlfwProcessEvents(void);

/* Set up standard GLFW callbacks for a window. */
MODULE_SCOPE void TkGlfwSetupCallbacks(
    GLFWwindow *glfwWindow,
    TkWindow   *tkWin);

/* Set up event loop notifier. */
void Tk_WaylandSetupTkNotifier(void);

/*
 *----------------------------------------------------------------------
 *
 * Colour Conversion Utilities
 *
 *----------------------------------------------------------------------
 */

/* Convert XColor to NVGcolor. */
MODULE_SCOPE NVGcolor TkGlfwXColorToNVG(XColor *xcolor);

/* Convert pixel value to NVGcolor. */
MODULE_SCOPE NVGcolor TkGlfwPixelToNVG(unsigned long pixel);

/* Apply GC settings to NanoVG context. */
MODULE_SCOPE void TkGlfwApplyGC(NVGcontext *vg, GC gc);

/*
 *----------------------------------------------------------------------
 *
 * GLFW Callback Functions
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void TkGlfwWindowCloseCallback(GLFWwindow *window);
MODULE_SCOPE void TkGlfwWindowSizeCallback(GLFWwindow *window,
    int width, int height);
MODULE_SCOPE void TkGlfwFramebufferSizeCallback(GLFWwindow *window,
    int width, int height);
MODULE_SCOPE void TkGlfwWindowPosCallback(GLFWwindow *window,
    int xpos, int ypos);
MODULE_SCOPE void TkGlfwWindowFocusCallback(GLFWwindow *window,
    int focused);
MODULE_SCOPE void TkGlfwWindowIconifyCallback(GLFWwindow *window,
    int iconified);
MODULE_SCOPE void TkGlfwWindowMaximizeCallback(GLFWwindow *window,
    int maximized);
MODULE_SCOPE void TkGlfwCursorPosCallback(GLFWwindow *window,
    double xpos, double ypos);
MODULE_SCOPE void TkGlfwMouseButtonCallback(GLFWwindow *window,
    int button, int action, int mods);
MODULE_SCOPE void TkGlfwScrollCallback(GLFWwindow *window,
    double xoffset, double yoffset);
MODULE_SCOPE void TkGlfwKeyCallback(GLFWwindow *window,
    int key, int scancode, int action, int mods);
MODULE_SCOPE void TkGlfwCharCallback(GLFWwindow *window,
    unsigned int codepoint);

/*
 *----------------------------------------------------------------------
 *
 * Keyboard Handling (xkbcommon integration)
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void TkWaylandUpdateKeyboardModifiers(int glfw_mods);
MODULE_SCOPE void TkWaylandStoreCharacterInput(unsigned int codepoint);

/*
 *----------------------------------------------------------------------
 *
 * Menu Support
 *
 *----------------------------------------------------------------------
 */

void TkWaylandMenuInit(void);

/*
 *----------------------------------------------------------------------
 *
 * Error Handling
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void TkGlfwErrorCallback(int error, const char *description);

/*
 *----------------------------------------------------------------------
 *
 * Xlib Emulation Layer
 *
 *	The following functions provide an Xlib-compatible API over the
 *	GLFW/NanoVG backend.  They are implemented in tkWaylandXlib.c.
 *
 *----------------------------------------------------------------------
 */

/* Creation / destruction. */

MODULE_SCOPE Window XCreateWindow(
    Display            *display,
    Window              parent,
    int                 x,
    int                 y,
    unsigned int        width,
    unsigned int        height,
    unsigned int        border_width,
    int                 depth,
    unsigned int        class_type,
    Visual             *visual,
    unsigned long       valuemask,
    XSetWindowAttributes *attributes);

MODULE_SCOPE Window XCreateSimpleWindow(
    Display     *display,
    Window       parent,
    int          x,
    int          y,
    unsigned int width,
    unsigned int height,
    unsigned int border_width,
    unsigned long border,
    unsigned long background);

MODULE_SCOPE int XDestroyWindow(Display *display, Window window);
MODULE_SCOPE int XDestroySubwindows(Display *display, Window window);

/* Mapping / visibility. */

MODULE_SCOPE int XMapWindow(Display *display, Window window);
MODULE_SCOPE int XMapRaised(Display *display, Window window);
MODULE_SCOPE int XMapSubwindows(Display *display, Window window);
MODULE_SCOPE int XUnmapWindow(Display *display, Window window);
MODULE_SCOPE int XUnmapSubwindows(Display *display, Window window);

/* Configuration. */

MODULE_SCOPE int XResizeWindow(
    Display     *display,
    Window       window,
    unsigned int width,
    unsigned int height);

MODULE_SCOPE int XMoveWindow(
    Display *display,
    Window   window,
    int      x,
    int      y);

MODULE_SCOPE int XMoveResizeWindow(
    Display     *display,
    Window       window,
    int          x,
    int          y,
    unsigned int width,
    unsigned int height);

MODULE_SCOPE int XConfigureWindow(
    Display         *display,
    Window           window,
    unsigned int     value_mask,
    XWindowChanges  *values);

MODULE_SCOPE int XSetWindowBorderWidth(
    Display     *display,
    Window       window,
    unsigned int width);

/* Stacking order. */

MODULE_SCOPE int XRaiseWindow(Display *display, Window window);
MODULE_SCOPE int XLowerWindow(Display *display, Window window);
MODULE_SCOPE int XCirculateSubwindowsUp(Display *display, Window window);
MODULE_SCOPE int XCirculateSubwindowsDown(Display *display, Window window);
MODULE_SCOPE int XRestackWindows(
    Display *display,
    Window  *windows,
    int      nwindows);

/* Window attributes. */

MODULE_SCOPE int XChangeWindowAttributes(
    Display              *display,
    Window                window,
    unsigned long         valuemask,
    XSetWindowAttributes *attributes);

MODULE_SCOPE int XSetWindowBackground(
    Display      *display,
    Window        window,
    unsigned long background);

MODULE_SCOPE int XSetWindowBackgroundPixmap(
    Display *display,
    Window   window,
    Pixmap   background_pixmap);

MODULE_SCOPE int XSetWindowBorder(
    Display      *display,
    Window        window,
    unsigned long border_pixel);

MODULE_SCOPE int XSetWindowBorderPixmap(
    Display *display,
    Window   window,
    Pixmap   border_pixmap);

/* Focus. */

MODULE_SCOPE int XSetInputFocus(
    Display *display,
    Window   focus,
    int      revert_to,
    Time     time);

/* ICCCM text properties. */

MODULE_SCOPE void XSetWMName(
    Display      *display,
    Window        window,
    XTextProperty *text_prop);

MODULE_SCOPE void XSetWMIconName(
    Display      *display,
    Window        window,
    XTextProperty *text_prop);

/*
 *----------------------------------------------------------------------
 *
 * GC / Pixmap Xlib Wrappers
 *
 *	These wrap the canonical TkWayland* implementations so that
 *	Xlib-style call sites continue to work unchanged.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE GC  XCreateGC(
    Display      *display,
    Drawable      d,
    unsigned long valuemask,
    XGCValues    *values);

MODULE_SCOPE int XFreeGC(Display *display, GC gc);
MODULE_SCOPE int XSetForeground(Display *display, GC gc, unsigned long fg);
MODULE_SCOPE int XSetBackground(Display *display, GC gc, unsigned long bg);
MODULE_SCOPE int XSetLineAttributes(
    Display     *display,
    GC           gc,
    unsigned int line_width,
    int          line_style,
    int          cap_style,
    int          join_style);
MODULE_SCOPE int XGetGCValues(
    Display      *display,
    GC            gc,
    unsigned long valuemask,
    XGCValues    *values);
MODULE_SCOPE int XChangeGC(
    Display      *display,
    GC            gc,
    unsigned long valuemask,
    XGCValues    *values);
MODULE_SCOPE int XCopyGC(
    Display      *display,
    GC            src,
    unsigned long valuemask,
    GC            dst);

MODULE_SCOPE Pixmap XCreatePixmap(
    Display     *display,
    Drawable     d,
    unsigned int width,
    unsigned int height,
    unsigned int depth);

MODULE_SCOPE int XFreePixmap(Display *display, Pixmap pixmap);

/*
 *----------------------------------------------------------------------
 *
 * Functions from the tkUnix source tree
 *
 *----------------------------------------------------------------------
 */

extern int Tktray_Init(Tcl_Interp *interp);
extern int SysNotify_Init(Tcl_Interp *interp);
extern int Cups_Init(Tcl_Interp *interp);
extern int TkAtkAccessibility_Init(Tcl_Interp *interp);

#endif /* _TKGLFWINT_H */

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
