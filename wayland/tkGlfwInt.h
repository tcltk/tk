/*
 * tkGlfwInt.h --
 *
 *	This file contains declarations that are shared among the
 *	GLFW/Wayland-specific parts of Tk.
 *
 * Copyright © 1995-1997 Sun Microsystems, Inc.
 * Copyright © 2026 Kevin Walzer
 * Copyright © 2026 Marc Culler. 
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
#include <libdecor.h>
#include <xkbcommon/xkbcommon.h>
#include "tkIntPlatDecls.h"
#include "tkWaylandDefaults.h"

#include "nanovg.h"

typedef struct NVGLUframebuffer NVGLUframebuffer;

/* Forward declarations for utils. */
NVGLUframebuffer* nvgluCreateFramebuffer(NVGcontext* ctx, int w, int h, int imageFlags);
void nvgluBindFramebuffer(NVGLUframebuffer* fb);
void nvgluDeleteFramebuffer(NVGLUframebuffer* fb);

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
    //// The rest of these fields are probably not needed.
    int nvgFrameActive;           /* Flag indicating if a NanoVG frame is
                                   * currently active */
    GLFWwindow *activeWindow;     /* Window that has the current active
                                   * NanoVG frame (if any) */
    int fbWidth;                  /* Framebuffer width of mainWindow
                                   * (cached for performance) */
    int fbHeight;                 /* Framebuffer height of mainWindow
                                   * (cached for performance) */
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
 * Each GLFWwindow has its WindowUserPointer set to the address of one of the
 * following structs.  This allows finding the TkWindow which wraps a given
 * GLFWWindow, as well as accessing other Tk specific data about the window.
 * The structs are also stored in a linked list so the setupProc or checkProc
 * can iterate through all GLFW windows in the application.
 */

/* Flag values */
#define needsDisplay 1
#define dontSwap     2
#define sizeChanged  4

typedef struct glfwTkInfo {
    GLFWwindow *glfwWindow;
    TkWindow *winPtr;
    TkGlfwContext context;
    unsigned int flags;
    struct glfwTkInfo *nextPtr;
} glfwTkInfo;


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
    int         isPixmap;   /* Set to 1 if drawing to an off-screen FBO, 0 for Window */
    GLuint      pixmapFbo;  /* Stores the active Pixmap FBO handle id */
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
 *	A Pixmap is a wrapper for a an NVGLUframebuffer.
 *
 *----------------------------------------------------------------------
 */

typedef struct TkWaylandPixmap {
    NVGLUframebuffer *fb;
    GLFWwindow *glfwWindow;  /* The window whose GL context has the fbo.   */
    int width;               /* It is simpler to cache the fb dimensions.  */
    int height;
    float pixelRatio;        /* We might support high-dpi pixmaps someday. */
} TkWaylandPixmap;


/*
 *----------------------------------------------------------------------
 *
 * The TkWindow structure contains a pointer to a struct TkWindowPrivate for
 * storing information specific to a port of Tk.  We use it for GLFW and
 * NVG objects associated to the window and for storing a string
 * for TkpGetString.
 *
 *----------------------------------------------------------------------
 */

typedef struct TkWindowPrivate {
    GLFWwindow *glfwWindow;
    NVGLUframebuffer *fb;
    Tcl_DString pendingText;
    float pixelRatio;
} glfwData;


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
 * Windows, Pixmaps, and Drawables
 *
 *----------------------------------------------------------------------
 */

Drawable TkWaylandDrawableForTkWindow(TkWindow *winPtr);
TkWindow* TkWaylandTkWindowFromDrawable(Drawable drawable);
Drawable TkWaylandDrawableForPixmap(TkWaylandPixmap *pixmap);
TkWaylandPixmap* TkWaylandPixmapFromDrawable(Drawable drawable);
bool TkWaylandDrawableIsPixmap(Drawable drawable);
Tk_Window GetToplevelOfWidget(Tk_Window tkwin);

/*
 *----------------------------------------------------------------------
 *
 * GLFW Initialization and Cleanup
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE int  TkGlfwInitialize(void);
MODULE_SCOPE void TkGlfwShutdown(ClientData clientData);

/*
 *----------------------------------------------------------------------
 *
 * Window Management
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE GLFWwindow *TkWaylandGetGLFWwindow(TkWindow *winPtr);

MODULE_SCOPE GLFWwindow *TkGlfwCreateWindow(
    TkWindow   *tkWin,
    int         width,
    int         height,
    const char *title,
    Drawable   *drawableOut);


MODULE_SCOPE void        TkGlfwDestroyWindow(GLFWwindow *glfwWindow);
MODULE_SCOPE Drawable    TkGlfwGetDrawable(GLFWwindow *w);
MODULE_SCOPE TkWindow*   TkGlfwGetTkWindow(GLFWwindow *glfwWindow);
MODULE_SCOPE GLFWwindow* TkWaylandGetGLFWwindowFromDrawable(Drawable drawable);
MODULE_SCOPE void        TkGlfwUpdateWindowSize(GLFWwindow *glfwWindow,
						int width, int height);
MODULE_SCOPE void        TkGlfwResizeWindow(GLFWwindow *w,
					    int width, int height);

/*
 *----------------------------------------------------------------------
 *
 * Drawing Context Management
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE int         TkGlfwBeginDraw(Drawable drawable, GC gc, TkWaylandDrawingContext *dcPtr);
MODULE_SCOPE void        TkGlfwEndDraw(TkWaylandDrawingContext *dcPtr);
MODULE_SCOPE NVGcontext *TkGlfwGetNVGContext(Drawable drawable);
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
 * Event Processing
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void TkGlfwProcessEvents(void);
MODULE_SCOPE void TkGlfwSetupCallbacks(GLFWwindow *glfwWindow);
MODULE_SCOPE void TkGlfwClearCallbacks(GLFWwindow *glfwWindow);
MODULE_SCOPE void Tk_WaylandSetupTkNotifier(void);
MODULE_SCOPE void TkWaylandQueueExposeEvent(TkWindow *winPtr, int x, int y,
					    int width, int height);
MODULE_SCOPE void TkWaylandWakeupGLFW(void);
MODULE_SCOPE void TkWaylandDisplayAllWindows(void);
MODULE_SCOPE KeySym TkWaylandGetKeysymFromScancode(int scancode);
void TkWaylandIbusSetCursorLocation(Tk_Window tkwin, int x, int y, int w, int h);

/* XKB keyboard state for key translation. */
typedef struct {
    struct xkb_context *context;
    struct xkb_keymap *keymap;
    struct xkb_state *state;
    struct xkb_compose_table *compose_table;
    struct xkb_compose_state *compose_state;
    uint32_t modifiers_depressed;
    uint32_t modifiers_latched;
    uint32_t modifiers_locked;
    uint32_t group;
} TkXKBState;


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
 * Keyboard Handling
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE int   TkWaylandKeyInit();
MODULE_SCOPE void  TkWaylandKeyCleanup();
MODULE_SCOPE void  TkWaylandUpdateKeyboardModifiers(int glfw_mods);
MODULE_SCOPE void  TkWaylandStoreText(TkWindow *winPtr, unsigned int codepoint);
char* TkWaylandGetStoredText(TkWindow *winPtr);
void TkWaylandSetStoredText(TkWindow *winPtr, const char *text);
MODULE_SCOPE void  TkWaylandClearStoredText(TkWindow *winPtr);

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
