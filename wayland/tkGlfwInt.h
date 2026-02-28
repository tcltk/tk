/*
 * tkGlfwInt.h --
 *
 *	This file contains declarations that are shared among the
 *	GLFW/Wayland-specific parts of Tk. Simplified version that
 *	relies on GLFW for Wayland protocol handling.
 *
 * Copyright © 1995-1997 Sun Microsystems, Inc.
 * Copyright © 2001-2009 Apple Inc.
 * Copyright © 2005-2009 Daniel A. Steffen <das@users.sourceforge.net>
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef _TKGLFWINT_H
#define _TKGLFWINT_H

#include "tkInt.h"
#include "tkUnixInt.h"

/* Must define this BEFORE including GLFW to get native Wayland functions. */
#define GLFW_EXPOSE_NATIVE_WAYLAND
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <GLES2/gl2.h>

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
    int	        nvgFrameActive;  /* Active frame */
    int         nvgFrameAutoOpened; /* Auto-opened frame */
    GLFWwindow *activeWindow;   /* Current window */
    struct wl_display *waylandDisplay;  /* Wayland display from GLFW */
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
 
typedef struct TkWaylandDecoration TkWaylandDecoration;
struct WmInfo;

typedef struct WindowMapping {
    TkWindow           *tkWindow;   /* Tk window pointer */
    GLFWwindow         *glfwWindow; /* Corresponding GLFW window */
    Drawable            drawable;   /* X11-style drawable ID */
    int                 width;      /* Current width */
    int                 height;     /* Current height */
    TkWaylandDecoration *decoration; /* Window decoration */
    struct wl_surface   *surface;   /* Wayland surface from GLFW */
    struct WindowMapping *nextPtr;  /* Next in linked list */
} WindowMapping;

/* Mapping functions */
WindowMapping *FindMappingByGLFW(GLFWwindow *glfwWindow);
WindowMapping *FindMappingByTk(TkWindow *tkWin);
WindowMapping *FindMappingByDrawable(Drawable drawable);
void           RemoveMapping(WindowMapping *mapping);
void           CleanupAllMappings(void);

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
    double alpha;       /* 0.0 = transparent, 1.0 = opaque. */
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
    int		 initialState;	/* NormalState, IconicState, WithdrawnState. */

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
    TkWaylandDecoration *decor; /* Client-side decoration. */
    struct TkWmInfo *nextPtr;
} WmInfo;

/*
 *----------------------------------------------------------------------
 *
 * Button state and type enums for decorations
 *
 *----------------------------------------------------------------------
 */
 
typedef enum {
    BUTTON_NORMAL,
    BUTTON_HOVER,
    BUTTON_PRESSED
} ButtonState;

typedef enum {
    BUTTON_CLOSE,
    BUTTON_MAXIMIZE,
    BUTTON_MINIMIZE
} ButtonType;

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandDecoration - client-side decoration structure.
 *
 *----------------------------------------------------------------------
 */
 
typedef struct TkWaylandDecoration {
    TkWindow *winPtr;
    GLFWwindow *glfwWindow;
    WmInfo    *wmPtr;           /* Pointer to the WM info for this window. */
    int enabled;
    int maximized;               /* Current maximized state (for button) */
    char *title;
    ButtonState closeState;
    ButtonState maxState;
    ButtonState minState;
    int dragging;                /* Unused - compositor manages drag */
    int resizing;                /* Unused - compositor manages resize */
} TkWaylandDecoration;

/* Decoration constants */
#define TITLE_BAR_HEIGHT    30
#define BORDER_WIDTH        1
#define BUTTON_WIDTH        30
#define BUTTON_HEIGHT       30
#define BUTTON_SPACING      5
#define CORNER_RADIUS       6.0f
#define SHADOW_BLUR         20.0f

#define RESIZE_NONE     0
#define RESIZE_LEFT     (1 << 0)
#define RESIZE_RIGHT    (1 << 1)
#define RESIZE_TOP      (1 << 2)
#define RESIZE_BOTTOM   (1 << 3)

/*
 *----------------------------------------------------------------------
 *
 * Decoration Functions
 *
 *----------------------------------------------------------------------
 */

TkWaylandDecoration *TkWaylandCreateDecoration(TkWindow *winPtr, GLFWwindow *glfwWindow);
void TkWaylandDestroyDecoration(TkWaylandDecoration *decor);
TkWaylandDecoration *TkWaylandGetDecoration(TkWindow *winPtr);
void TkWaylandSetDecorationTitle(TkWaylandDecoration *decor, const char *title);
void TkWaylandSetWindowMaximized(TkWaylandDecoration *decor, int maximized);
void TkWaylandDrawDecoration(TkWaylandDecoration *decor, NVGcontext *vg);
int TkWaylandDecorationMouseMove(TkWaylandDecoration *decor, double x, double y);
int TkWaylandDecorationMouseButton(TkWaylandDecoration *decor, int button, int action, double x, double y);
void TkWaylandConfigureWindowDecorations(void);
int TkWaylandShouldUseCSD(void);
void TkWaylandInitDecorationPolicy(Tcl_Interp *interp);

/*
 *----------------------------------------------------------------------
 *
 * Drawing Context Structure
 *
 *----------------------------------------------------------------------
 */

typedef struct {
    NVGcontext *vg;         /* NanoVG context for this draw */
    Drawable    drawable;   /* Target drawable */
    GLFWwindow *glfwWindow; /* Associated GLFW window */
    int         width;      /* Drawable width */
    int         height;     /* Drawable height */
    int         nestedFrame; /* Frame within frame */
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

MODULE_SCOPE TkGlfwContext *TkGlfwGetContext(void);

/*
 *----------------------------------------------------------------------
 *
 * Initialization and Cleanup
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE int  TkGlfwInitialize(void);
MODULE_SCOPE void TkGlfwCleanup(void);

/*
 *----------------------------------------------------------------------
 *
 * Window Management
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE GLFWwindow *TkGlfwCreateWindow(
    TkWindow   *tkWin,
    int         width,
    int         height,
    const char *title,
    Drawable   *drawableOut);

MODULE_SCOPE void TkGlfwDestroyWindow(GLFWwindow *glfwWindow);
MODULE_SCOPE GLFWwindow *TkGlfwGetGLFWWindow(Tk_Window tkwin);
MODULE_SCOPE TkWindow   *TkGlfwGetTkWindow(GLFWwindow *glfwWindow);
MODULE_SCOPE GLFWwindow *TkGlfwGetWindowFromDrawable(Drawable drawable);
MODULE_SCOPE void TkGlfwUpdateWindowSize(GLFWwindow *glfwWindow, int width, int height);

/*
 *----------------------------------------------------------------------
 *
 * Drawing Context Management
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE int  TkGlfwBeginDraw(
    Drawable                drawable,
    GC                      gc,
    TkWaylandDrawingContext *dcPtr);

MODULE_SCOPE void TkGlfwEndDraw(TkWaylandDrawingContext *dcPtr);
MODULE_SCOPE NVGcontext *TkGlfwGetNVGContext(void);
MODULE_SCOPE void TkGlfwFlushAutoFrame(void);
MODULE_SCOPE NVGcontext *TkGlfwGetNVGContextForMeasure(void);

/*
 *----------------------------------------------------------------------
 *
 * GC Internals
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE GC   TkWaylandCreateGC(unsigned long valuemask, XGCValues *values);
MODULE_SCOPE void TkWaylandFreeGC(GC gc);
MODULE_SCOPE int  TkWaylandGetGCValues(GC gc, unsigned long valuemask, XGCValues *values);
MODULE_SCOPE int  TkWaylandChangeGC(GC gc, unsigned long valuemask, XGCValues *values);
MODULE_SCOPE int  TkWaylandCopyGC(GC src, unsigned long valuemask, GC dst);

/*
 *----------------------------------------------------------------------
 *
 * Pixmap Internals
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE Pixmap TkWaylandCreatePixmap(int width, int height, int depth);
MODULE_SCOPE void TkWaylandFreePixmap(Pixmap pixmap);
MODULE_SCOPE int       TkWaylandGetPixmapImageId(Pixmap pixmap);
MODULE_SCOPE NVGpaint *TkWaylandGetPixmapPaint(Pixmap pixmap);
MODULE_SCOPE int       TkWaylandGetPixmapType(Pixmap pixmap);
MODULE_SCOPE void      TkWaylandGetPixmapDimensions(Pixmap pixmap, int *width, int *height, int *depth);
MODULE_SCOPE int TkWaylandUpdatePixmapImage(Pixmap pixmap, const unsigned char *data);
MODULE_SCOPE void TkWaylandCleanupPixmapStore(void);
MODULE_SCOPE void       TkWaylandSetNVGContext(NVGcontext *vg);
MODULE_SCOPE NVGcontext *TkWaylandGetPixmapNVGContext(void);

/*
 *----------------------------------------------------------------------
 *
 * Event Processing
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void TkGlfwProcessEvents(void);
MODULE_SCOPE void TkGlfwSetupCallbacks(GLFWwindow *glfwWindow, TkWindow *tkWin);
void Tk_WaylandSetupTkNotifier(void);

/* Expose event structure */
typedef struct {
    Tcl_Event      header;   /* Must be first. */
    XEvent         xEvent;
    TkWindow      *winPtr;
} TkWaylandExposeEvent;

void TkWaylandQueueExposeEvent(TkWindow *winPtr, int x, int y, int width, int height);

/*
 *----------------------------------------------------------------------
 *
 * Color Conversion Utilities
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE NVGcolor TkGlfwXColorToNVG(XColor *xcolor);
MODULE_SCOPE NVGcolor TkGlfwPixelToNVG(unsigned long pixel);
MODULE_SCOPE void TkGlfwApplyGC(NVGcontext *vg, GC gc);

/*
 *----------------------------------------------------------------------
 *
 * GLFW Callback Functions
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void TkGlfwWindowCloseCallback(GLFWwindow *window);
MODULE_SCOPE void TkGlfwWindowSizeCallback(GLFWwindow *window, int width, int height);
MODULE_SCOPE void TkGlfwFramebufferSizeCallback(GLFWwindow *window, int width, int height);
MODULE_SCOPE void TkGlfwWindowPosCallback(GLFWwindow *window, int xpos, int ypos);
MODULE_SCOPE void TkGlfwWindowFocusCallback(GLFWwindow *window, int focused);
MODULE_SCOPE void TkGlfwWindowIconifyCallback(GLFWwindow *window, int iconified);
MODULE_SCOPE void TkGlfwWindowMaximizeCallback(GLFWwindow *window, int maximized);
MODULE_SCOPE void TkGlfwCursorPosCallback(GLFWwindow *window, double xpos, double ypos);
MODULE_SCOPE void TkGlfwMouseButtonCallback(GLFWwindow *window, int button, int action, int mods);
MODULE_SCOPE void TkGlfwScrollCallback(GLFWwindow *window, double xoffset, double yoffset);
MODULE_SCOPE void TkGlfwKeyCallback(GLFWwindow *window, int key, int scancode, int action, int mods);
MODULE_SCOPE void TkGlfwCharCallback(GLFWwindow *window, unsigned int codepoint);
MODULE_SCOPE void TkGlfwWindowRefreshCallback(GLFWwindow *window);

/*
 *----------------------------------------------------------------------
 *
 * Keyboard Handling
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
 *----------------------------------------------------------------------
 */

MODULE_SCOPE Window XCreateSimpleWindow(Display *display, Window parent, int x, int y,
    unsigned int width, unsigned int height, unsigned int border_width,
    unsigned long border, unsigned long background);
MODULE_SCOPE int XDestroySubwindows(Display *display, Window window);
MODULE_SCOPE int XMapRaised(Display *display, Window window);
MODULE_SCOPE int XMapSubwindows(Display *display, Window window);
MODULE_SCOPE int XUnmapSubwindows(Display *display, Window window);
MODULE_SCOPE int XCirculateSubwindowsUp(Display *display, Window window);
MODULE_SCOPE int XCirculateSubwindowsDown(Display *display, Window window);
MODULE_SCOPE int XRestackWindows(Display *display, Window *windows, int nwindows);
MODULE_SCOPE void XSetWMName(Display *display, Window window, XTextProperty *text_prop);
MODULE_SCOPE void XSetWMIconName(Display *display, Window window, XTextProperty *text_prop);

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
