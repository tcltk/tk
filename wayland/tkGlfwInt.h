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
    bool	    nvgFrameActive   /* Active frame */
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
    TkWaylandDecoration *decoration; /* Window decoration. */ 
    struct WindowMapping *nextPtr;  /* Next in linked list */
} WindowMapping;

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
    int			initialState;	/* NormalState, IconicState, WithdrawnState */

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
	TkWaylandDecoration *decor; /* Client-side decoration. */

    struct TkWmInfo *nextPtr;
} WmInfo;

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
 * TkWaylandDecoration - client-side decoration structure and functions. 
 *
 *----------------------------------------------------------------------
 */
 
typedef struct TkWaylandDecoration {
    TkWindow *winPtr;
    GLFWwindow *glfwWindow;
    WmInfo    *wmPtr;           /* Pointer to the WM info for this window */
    int enabled;
    int maximized;               /* Current maximized state (for button) */
    char *title;
    ButtonState closeState;
    ButtonState maxState;
    ButtonState minState;
    int dragging;
    double dragStartX, dragStartY;
    int windowStartX, windowStartY;
    int resizing;
    double resizeStartX, resizeStartY;
    int resizeStartWidth, resizeStartHeight;
} TkWaylandDecoration;

TkWaylandDecoration *TkWaylandGetDecoration(TkWindow *winPtr);
void TkWaylandSetDecorationTitle(TkWaylandDecoration *decor, const char *title);
void TkWaylandSetWindowMaximized(TkWaylandDecoration *decor, int maximized);
void TkWaylandConfigureWindowDecorations(void);
int TkWaylandShouldUseCSD(void);
TkWaylandDecoration *TkWaylandCreateDecoration(TkWindow *winPtr, GLFWwindow *glfwWindow); 
void TkWaylandInitDecorationPolicy(Tcl_Interp *interp); 
TkWaylandDecoration *TkWaylandCreateDecoration(TkWindow *winPtr, GLFWwindow *glfwWindow); 
void TkWaylandDestroyDecoration(TkWaylandDecoration *decor); 
TkWaylandDecoration *TkWaylandGetDecoration(TkWindow *winPtr);
void TkWaylandDrawDecoration(TkWaylandDecoration *decor, NVGcontext *vg);

/* Decoration constants. */
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
MODULE_SCOPE void TkGlfwWindowRefreshCallback(GLFWwindow *window);
MODULE_SCOPE void TkGlfwWindowSizeCallback(
    GLFWwindow *window, int width, int height);     

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

MODULE_SCOPE int XDestroySubwindows(Display *display, Window window);

/* Mapping / visibility. */

MODULE_SCOPE int XMapRaised(Display *display, Window window);
MODULE_SCOPE int XMapSubwindows(Display *display, Window window);
MODULE_SCOPE int XUnmapSubwindows(Display *display, Window window);
MODULE_SCOPE int XCirculateSubwindowsUp(Display *display, Window window);
MODULE_SCOPE int XCirculateSubwindowsDown(Display *display, Window window);
MODULE_SCOPE int XRestackWindows(
    Display *display,
    Window  *windows,
    int      nwindows);

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
