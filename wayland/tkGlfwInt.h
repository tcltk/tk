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
#include <libdecor.h>
#include "tkIntPlatDecls.h"
#include "tkWaylandDefaults.h"

#include "nanovg.h"
#define NANOVG_GLES2_IMPLEMENTATION
#include "nanovg_gl.h"
#include "nanovg_gl_utils.h"

/*
 *----------------------------------------------------------------------
 *
 * Window Mapping Structure
 *
 *	Maps between Tk windows, GLFW windows, and drawable IDs.
 *	Each toplevel window has one WindowMapping structure.
 *	Child windows and pixmaps share the mapping of their parent toplevel.
 *
 *----------------------------------------------------------------------
 */

struct WmInfo;

typedef struct WindowMapping {
    TkWindow *tkWindow;           /* Associated Tk window (may be NULL for
                                   * offscreen/pixmap-only mappings) */
    GLFWwindow *glfwWindow;        /* Associated GLFW window - all drawing
                                   * for this mapping goes to this window */
    Drawable drawable;              /* Tk drawable ID for this toplevel.
                                   * Child windows and pixmaps that share
                                   * this mapping have their own drawable IDs
                                   * registered separately via DrawableMapping */
    int width;                      /* Current window width in pixels
                                   * (updated by configure events) */
    int height;                     /* Current window height in pixels
                                   * (updated by configure events) */
    int clearPending;               /* Flag indicating the framebuffer needs
                                   * to be cleared before next draw operation.
                                   * Set to 1 when window is created/resized,
                                   * cleared after clearing */
    int swapPending;   	           /* 1 = buffer is ready to swap at next idle */
    int frameOpen;                /* Is NVG frame currently open? */
    int needsDisplay;             /* Dirty flag - needs redraw */
    int inEventCycle;             /* Currently processing events */
    NVGLUframebuffer *fbo;		 /* NanoVG frame buffer. */	
    struct WindowMapping *nextPtr;  /* Next mapping in global linked list */
} WindowMapping;

/*
 *----------------------------------------------------------------------
 *
 * Drawable Mapping Structure
 *
 *	Maps arbitrary drawable IDs (child windows, pixmaps) to their
 *	parent WindowMapping. This allows TkGlfwBeginDraw to find the
 *	correct GLFW window and toplevel for any drawable.
 *
 *----------------------------------------------------------------------
 */

typedef struct DrawableMapping {
    Drawable drawable;              /* Tk drawable ID (child window or pixmap) */
    WindowMapping *mapping;         /* Pointer to the parent WindowMapping
                                   * that owns this drawable */
    struct DrawableMapping *next;   /* Next in global linked list */
} DrawableMapping;

/*
 *----------------------------------------------------------------------
 *
 * Core Context Structure
 *
 *	Global state for the GLFW/Wayland backend.
 *	This structure holds the shared GL context window, the global
 *	NanoVG context, and state tracking for nested drawing operations.
 *
 *----------------------------------------------------------------------
 */

typedef struct TkGlfwContext {
    GLFWwindow *mainWindow;      /* Hidden shared context window - all
                                   * application windows share this context */
    NVGcontext *vg;               /* Global NanoVG context - created once
                                   * and shared by all windows */
    int initialized;              /* GLFW initialized flag - 1 if glfwInit()
                                   * has been called successfully */
    int nvgFrameActive;           /* Flag indicating if a NanoVG frame is
                                   * currently active */
    GLFWwindow *activeWindow;     /* Window that has the current active
                                   * NanoVG frame (if any) */
    int fbWidth;                  /* Framebuffer width of mainWindow
                                   * (cached for performance) */
    int fbHeight;                 /* Framebuffer height of mainWindow
                                   * (cached for performance) */
    WindowMapping *activeFrame;      /* Which window has open frame */
} TkGlfwContext;

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
    int          initialState;  /* NormalState, IconicState, WithdrawnState */

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
    WmAttributes  attributes;
    WmAttributes  reqState;
    ProtocolHandler *protPtr;
    Tcl_Size      cmdArgc;
    Tcl_Obj     **cmdArgv;
    char         *clientMachine;
    int           flags;
    int           numTransients;
    int           iconDataSize;
    unsigned char *iconDataPtr;
    GLFWimage    *glfwIcon;
    int           glfwIconCount;
    int           isMapped;
    int           lastX, lastY;
    int           lastWidth, lastHeight;
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
    NVGcontext *vg;          /* NanoVG context for this draw */
    Drawable    drawable;    /* Target drawable */
    GLFWwindow *glfwWindow;  /* Associated GLFW window */
    int         width;       /* Drawable width */
    int         height;      /* Drawable height */
    int         offsetX;     /* Offset of child widget */
    int         offsetY;     /* Offset of child widget */
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
#define TK_WAYLAND_PIXMAP_MAGIC 0x544B5058  /* "TKPX" */

typedef struct TkWaylandPixmapImpl {
	int      		 magic;        
    int              type;          /* 0 = window, 1 = pixmap */
    int              width;
    int              height;
    Drawable         drawable;      /* Tk's drawable ID */
    
    /* OpenGL FBO resources */
    GLuint           fbo;           /* Framebuffer object */
    GLuint           texture;       /* Color attachment */
    GLuint           stencil;       /* Stencil buffer (for NanoVG) */
    
    /* NanoVG frame state */
    int              frameOpen;     /* Is NVG frame open on this pixmap? */
    
    /* Associated window (for context) */
    WindowMapping   *windowMapping; /* Which window owns this pixmap */
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
 * Decoration constants
 *
 *----------------------------------------------------------------------
 */

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
MODULE_SCOPE void TkGlfwShutdown(ClientData clientData);

/*
 *----------------------------------------------------------------------
 *
 * Window Mapping Management
 *
 *----------------------------------------------------------------------
 */

/* Add a new mapping to the list. */
void AddMapping(WindowMapping *mapping);

/* Find mapping by GLFW window. */
MODULE_SCOPE WindowMapping *FindMappingByGLFW(GLFWwindow *w);

/* Find mapping by Tk window. */
MODULE_SCOPE WindowMapping *FindMappingByTk(TkWindow *w);

/* Find mapping by drawable. */
MODULE_SCOPE WindowMapping *FindMappingByDrawable(Drawable d);

/* Get the head of the mapping list. */
MODULE_SCOPE WindowMapping *TkGlfwGetMappingList(void);

/* Remove a mapping from the list. */
void RemoveMapping(WindowMapping *m);

/* Clean up all mappings. */
MODULE_SCOPE void CleanupAllMappings(void);

/* Register a drawable with a mapping. */
MODULE_SCOPE void RegisterDrawableForMapping(Drawable d, WindowMapping *m);

/* Get toplevel of a widget. */
MODULE_SCOPE Tk_Window GetToplevelOfWidget(Tk_Window tkwin);

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

MODULE_SCOPE void        TkGlfwDestroyWindow(GLFWwindow *glfwWindow);
MODULE_SCOPE GLFWwindow *TkGlfwGetGLFWWindow(Tk_Window tkwin);
MODULE_SCOPE Drawable    TkGlfwGetDrawable(GLFWwindow *w);
MODULE_SCOPE TkWindow   *TkGlfwGetTkWindow(GLFWwindow *glfwWindow);
MODULE_SCOPE GLFWwindow *TkGlfwGetWindowFromDrawable(Drawable drawable);
MODULE_SCOPE void        TkGlfwUpdateWindowSize(GLFWwindow *glfwWindow, int width, int height);
MODULE_SCOPE void        TkGlfwResizeWindow(GLFWwindow *w, int width, int height);

/*
 *----------------------------------------------------------------------
 *
 * Drawing Context Management
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE int         TkGlfwBeginDraw(Drawable drawable, GC gc, TkWaylandDrawingContext *dcPtr);
MODULE_SCOPE void        TkGlfwEndDraw(TkWaylandDrawingContext *dcPtr);
MODULE_SCOPE NVGcontext *TkGlfwGetNVGContext(void);
MODULE_SCOPE NVGcontext *TkGlfwGetNVGContextForMeasure(void);
int IsPixmap(Drawable drawable);

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
 * Event Processing
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void TkGlfwProcessEvents(void);
MODULE_SCOPE void TkGlfwSetupCallbacks(GLFWwindow *glfwWindow, TkWindow *tkWin);
MODULE_SCOPE void Tk_WaylandSetupTkNotifier(void);
MODULE_SCOPE void SyncWindowSize(WindowMapping *m);

typedef struct {
    Tcl_Event  header;  /* Must be first. */
    XEvent     xEvent;
    TkWindow  *winPtr;
} TkWaylandExposeEvent;

MODULE_SCOPE void TkWaylandQueueExposeEvent(TkWindow *winPtr, int x, int y, int width, int height);
MODULE_SCOPE void TkWaylandHandleExposeEvents(void);
MODULE_SCOPE void TkWaylandBeginEventCycle(WindowMapping *m);
MODULE_SCOPE void TkWaylandEndEventCycle(WindowMapping *m);
void TkWaylandScheduleDisplay(WindowMapping *m);
void TkWaylandDisplayProc(ClientData clientData);
MODULE_SCOPE void TkWaylandWakeupGLFW(void);


/*
 *----------------------------------------------------------------------
 *
 * Color Conversion Utilities
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE NVGcolor TkGlfwXColorToNVG(XColor *xcolor);
MODULE_SCOPE NVGcolor TkGlfwPixelToNVG(unsigned long pixel);
MODULE_SCOPE void     TkGlfwApplyGC(NVGcontext *vg, GC gc);

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

MODULE_SCOPE void TkWaylandMenuInit(void);

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

MODULE_SCOPE int Tktray_Init(Tcl_Interp *interp);
MODULE_SCOPE int SysNotify_Init(Tcl_Interp *interp);
MODULE_SCOPE int Cups_Init(Tcl_Interp *interp);
MODULE_SCOPE int TkWaylandAccessibility_Init(Tcl_Interp *interp);

#endif /* _TKGLFWINT_H */

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
