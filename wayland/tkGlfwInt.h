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
#include "tkMenu.h"
#include "tkMenubutton.h"  /* Add this line */
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
 * Opaque forward declarations for the native Wayland popup primitive
 * (implemented in tkWaylandPopup.c) and the Wayland seat object (from
 * wayland-client.h, not included here to keep this header decoupled
 * from the raw Wayland protocol headers).
 */
typedef struct TkWaylandPopup TkWaylandPopup;
struct wl_seat;

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

    /* Native Wayland popup surfaces (tkWaylandPopup.c). */
    TkWaylandPopup *popup;          /* Active xdg_popup for OR / menu
                                      * windows; NULL otherwise. */
    TkWaylandPopup *menubarPopup;   /* Popup surface for the menubar
                                      * strip, if any. */
    int          overrideRedirect;  /* Mirrors wm overrideredirect /
                                      * TkpMakeMenuWindow. */

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

typedef struct TkWaylandGC {
    unsigned long foreground; /* Foreground color (pixel value) */
    unsigned long background; /* Background color (pixel value) */
    int           line_width; /* Line width in pixels */
    int           line_style; /* LineSolid, LineOnOffDash, etc. */
    int           cap_style;  /* CapButt, CapRound, CapProjecting */
    int           join_style; /* JoinMiter, JoinRound, JoinBevel */
    int           fill_rule;  /* EvenOddRule or WindingRule */
    int           arc_mode;   /* ArcChord or ArcPieSlice */
    void         *font;       /* Font handle (reserved) */
} TkWaylandGC;

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
	unsigned long id;   	/* Explicit unique XID */
    NVGLUframebuffer *fb;
    GLFWwindow *glfwWindow;  /* The window whose GL context has the fbo.   */
    int width;               /* It is simpler to cache the fb dimensions.  */
    int height;
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

/*
 * Hit-testing and pointer-serial support for popup/menu dispatch.
 * Implemented in tkGlfwInit.c.
 */
MODULE_SCOPE TkWindow  *TkWaylandWindowAtPos(GLFWwindow *glfwWindow,
					     int x, int y);
MODULE_SCOPE void        TkWaylandRegisterPointerListener(void);

/*
 * Post a virtual event (e.g. "<<MenuDone>>") to a Tk window's event
 * queue.  Used to defer Tk-level cleanup (menu unposting, etc.) from
 * Wayland protocol callbacks to the normal Tcl event loop.  Implemented
 * in tkGlfwInit.c.
 */
MODULE_SCOPE void TkWaylandPostVirtualEvent(TkWindow *winPtr,
					    const char *eventName);

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
int TkWaylandIbusIsComposing(Tk_Window);
MODULE_SCOPE void  TkWaylandClearStoredText(TkWindow *winPtr);

/*
 *----------------------------------------------------------------------
 *
 * Popup Support
 *
 *	Native xdg_popup primitive (tkWaylandPopup.c).  This is the base
 *	object used by menus, combobox dropdowns, tooltips, and any other
 *	override-redirect surface.  The TkWaylandPopup struct itself is
 *	opaque; all access goes through these functions.
 *
 *----------------------------------------------------------------------
 */

/* Lifecycle */
MODULE_SCOPE int   TkWaylandPopupInit(void);
MODULE_SCOPE void  TkWaylandPopupDestroyAll(void);

/*
 * Create a popup.
 *
 *   parentGlfw  – GLFW window whose wl_surface is the xdg_popup parent.
 *   anchorX,Y   – top-left of the anchor rectangle, in parent-surface
 *                 logical pixels.
 *   anchorW,H   – anchor rectangle size (1,1 for a point anchor).
 *   popupW,H    – requested popup size.
 *   anchor      – XDG_POSITIONER_ANCHOR_* value.
 *   gravity     – XDG_POSITIONER_GRAVITY_* value.
 *   grabInput   – non-zero to take an explicit Wayland pointer grab.
 *   serial      – input-event serial for the grab (0 if grabInput==0).
 */
MODULE_SCOPE TkWaylandPopup *TkWaylandPopupCreate(
    GLFWwindow *parentGlfw,
    int anchorX, int anchorY,
    int anchorW, int anchorH,
    int popupW,  int popupH,
    uint32_t anchor,
    uint32_t gravity,
    int grabInput,
    uint32_t serial);

MODULE_SCOPE void TkWaylandPopupDestroy(TkWaylandPopup *popup);

/*
 * Reposition by destroying and re-creating with new positioner
 * parameters.  Returns the new popup; the original is freed.
 */
MODULE_SCOPE TkWaylandPopup *TkWaylandPopupMove(
    TkWaylandPopup *popup,
    GLFWwindow     *parentGlfw,
    int anchorX, int anchorY,
    int anchorW, int anchorH,
    uint32_t anchor,
    uint32_t gravity);

/* Rendering */
MODULE_SCOPE NVGcontext *TkWaylandPopupGetNVGContext(TkWaylandPopup *popup);
MODULE_SCOPE int          TkWaylandPopupBeginDraw(TkWaylandPopup *popup);
MODULE_SCOPE void         TkWaylandPopupEndDraw(TkWaylandPopup *popup);

/* Callbacks and queries */
MODULE_SCOPE void TkWaylandPopupSetDoneCallback(
    TkWaylandPopup *popup,
    void (*callback)(void *clientData),
    void *clientData);

MODULE_SCOPE void     TkWaylandPopupSetSerial(uint32_t serial);
MODULE_SCOPE uint32_t TkWaylandPopupLastSerial(void);
MODULE_SCOPE void     TkWaylandPopupGetSize(
    TkWaylandPopup *popup, int *widthOut, int *heightOut);
MODULE_SCOPE void     TkWaylandPopupGetPosition(
    TkWaylandPopup *popup, int *xOut, int *yOut);
MODULE_SCOPE struct wl_seat *TkWaylandPopupGetSeat(void);

/*
 * Subsurface-mode popups (wl_subsurface, not xdg_popup).  Use for
 * surfaces that are a permanent part of a window -- e.g. the menubar
 * strip -- as opposed to transient, compositor-dismissed surfaces
 * (use TkWaylandPopupCreate / xdg_popup for those).  No configure
 * handshake; usable immediately after creation.
 */
MODULE_SCOPE TkWaylandPopup *TkWaylandSubsurfaceCreate(
    GLFWwindow *parentGlfw,
    int x, int y, int width, int height);
MODULE_SCOPE void TkWaylandSubsurfaceReconfigure(
    TkWaylandPopup *popup,
    int x, int y, int width, int height);
MODULE_SCOPE void TkWaylandSubsurfacePlaceAbove(
    TkWaylandPopup *popup, TkWaylandPopup *sibling);

/*
 *----------------------------------------------------------------------
 *
 * Menu Support
 *
 *	Dropdown/cascade menus are implemented as a stack of
 *	wl_subsurface-backed popups (TkWaylandSubsurfaceCreate) with empty
 *	input regions, so all pointer/keyboard input continues to arrive at
 *	the toplevel's raw wl_pointer / wl_keyboard listeners
 *	(tkGlfwInit.c) in toplevel-surface-local coordinates.  Those
 *	listeners call the Handle* functions below, which perform hit
 *	testing against the menu stack and dispatch to the appropriate
 *	TkMenu, or dismiss the stack on an outside click / Escape.
 *
 *	This design is self-contained: it does not depend on or interact
 *	with GLFW's own per-window callbacks (registered elsewhere via
 *	TkGlfwSetupCallbacks).
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void TkWaylandMenuInit(void);

/* Non-zero if one or more menu popups are currently posted. */
MODULE_SCOPE int  TkWaylandMenuPopupActive(void);

/*
 * Post a menu as either the root of a new menu stack (isRoot != 0,
 * dismisses any existing stack first) or as a cascade one level deeper
 * than the current top of stack (isRoot == 0).  anchorX/Y/W/H are in
 * toplevel-surface-local coordinates; the popup is placed below-left of
 * the anchor by default, flipping above/left if it would not fit within
 * the toplevel's current size.  popupW/H are the menu's natural size.
 *
 * Returns TCL_OK / TCL_ERROR.
 */
MODULE_SCOPE int  TkWaylandPostMenuAtAnchor(
    Tcl_Interp *interp, TkMenu *menuPtr,
    int anchorX, int anchorY, int anchorW, int anchorH,
    int popupW, int popupH,
    int isRoot);

/* Dismiss the entire menu stack (all cascades + the root menu). */
MODULE_SCOPE void TkWaylandMenuDismissAll(void);

/*
 * Raw input dispatch, called from tkGlfwInit.c's wl_pointer / wl_keyboard
 * listeners.  x, y are toplevel-surface-local logical pixels.
 * state follows the wl_pointer_button_state / wl_keyboard_key_state enum
 * (0 = released, 1 = pressed).
 */
MODULE_SCOPE void TkWaylandMenuHandlePointerMotion(int x, int y);
MODULE_SCOPE void TkWaylandMenuHandlePointerButton(int x, int y,
				   int button, int state);
MODULE_SCOPE void TkWaylandMenuHandleEscape(void);

/*
 * Returns and clears a one-shot flag set when the most recent button
 * press dismissed the menu stack via an outside click.  Whatever file
 * registers the GLFW mouse-button callback may call this to swallow that
 * click rather than also activating a widget underneath.
 */
MODULE_SCOPE int  TkWaylandMenuConsumeDismissClick(void);

/*
 *----------------------------------------------------------------------
 *
 * Menubutton Support
 *
 *	Posting entry points implemented in tkWaylandMenubu.c, called from
 *	the GLFW mouse-button dispatcher in tkGlfwInit.c.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void TkpMenuButtonMaybePost(TkWindow *winPtr);
MODULE_SCOPE int  TkpMenuButtonPostMenu(TkMenuButton *mbPtr);

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
