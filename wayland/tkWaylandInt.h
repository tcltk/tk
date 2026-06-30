/*
 * tkWaylandInt.h --
 *
 *	This file contains declarations that are shared among the
 *	GLFW/Wayland-specific parts of Tk.
 *
 * Copyright © 1995-1997 Sun Microsystems, Inc.
 * Copyright © 2026 Kevin Walzer
 * Copyright © 2026 Marc Culler
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef _TkWaylandINT_H
#define _TkWaylandINT_H

#include "tkInt.h"
#include "tkUnixInt.h"
#include "tkFont.h"
#include "tkMenu.h"
#include "tkMenubutton.h"
#include <GLFW/glfw3.h>
#include <GLES3/gl3.h>
#include <libdecor.h>
#include <xkbcommon/xkbcommon.h>
#include "tkIntPlatDecls.h"
#include "tkWaylandDefaults.h"

#include <fontconfig/fontconfig.h>
#include <hb.h>
#include <SheenBidi/SheenBidi.h>

#include <systemd/sd-bus.h>

#ifdef SD_BUS_ERROR_NULL
extern int    ibus_fd;
extern sd_bus *ibus_bus;
#endif

/*
 * NanoVG headers - MUST be included before any usage of NVGcontext or NVGcolor.
 */
#define NANOVG_GLES3 1
#include "nanovg.h"
#include "nanovg_gl.h"
#include "nanovg_gl_utils.h"

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
 * Font System Structures (from tkWaylandFont.c)
 *
 *	Shared font structures for HarfBuzz shaping, Fontconfig fallback,
 *	and NanoVG rendering.
 *
 *----------------------------------------------------------------------
 */

/* Tuning constants for font shaping */
#define MAX_FACES           64      /* Max fallback faces per logical font.  */
#define MAX_GLYPHS          512     /* Max glyphs per shaped buffer.         */
#define MAX_BIDI_RUNS       32      /* Max bidi level runs.                  */
#define MAX_CLUSTER_BREAKS  512     /* Max cluster break positions.          */
#define MAX_STRING_CACHE    1024    /* Max bytes stored in one cache slot.   */
#define CACHE_SLOTS         8       /* Number of LRU shaper cache entries.   */

/*
 * BidiRun
 *
 *   One bidirectional level run produced by SheenBidi.  Offsets are in
 *   UCS-4 character indices (not bytes); the shaper converts to bytes.
 */
typedef struct {
    int offset;     /* Character index of run start in the decoded array. */
    int len;        /* Length in characters.                              */
    int isRTL;      /* 1 = right-to-left, 0 = left-to-right.             */
} BidiRun;

/*
 * WaylandFtFace
 *
 *   One font face from Fontconfig, with lazily-loaded HarfBuzz state and
 *   the NanoVG font id for this face.
 */
typedef struct WaylandFtFace {
    FcPattern  *source;         /* FC pattern — owned by fontset, not us.   */
    FcCharSet  *charset;        /* Character coverage — owned by us.        */

    /* HarfBuzz state (loaded lazily on first shape call). */
    hb_font_t  *hbFont;
    hb_blob_t  *hbBlob;
    hb_face_t  *hbFace;
    int         isLoaded;       /* Non-zero once hbFont is ready.           */

    /* NanoVG handle for this face (loaded lazily on first draw). */
    char        nvgName[64];    /* Unique name used with nvgCreateFont.     */
    int         nvgFontId;      /* -1 until loaded.                         */
    char       *filePath;       /* Font file path (strdup'd).               */
    int         faceIndex;      /* Index within font file (for TTC etc.).   */

    /* Metrics (filled when hbFont is loaded). */
    double      unitsPerEm;
    double      ascender;
    double      descender;
} WaylandFtFace;

/*
 * ShapedGlyphBuffer
 *
 *   Output of WaylandShaper_ShapeString.  Contains per-glyph advance and
 *   cluster information for measuring and rendering.
 */
typedef struct ShapedGlyphBuffer {
    struct {
        int          faceIndex;   /* Which WaylandFtFace produced this glyph. */
        unsigned int glyphId;     /* HarfBuzz glyph id.                       */
        int          x;           /* Visual X offset from string origin (px).  */
        int          y;           /* Vertical offset (pixels, usually 0).      */
        int          advanceX;    /* Width in pixels.                          */
        int          byteOffset;  /* Byte offset in source UTF-8 string.       */
        int          clusterLen;  /* Bytes in this cluster.                    */
        int          isRTL;       /* 1 if from an RTL run.                     */

        /* UTF-8 for this cluster (for nvgText rendering). */
        char         clusterUtf8[16];
        int          clusterUtf8Len;
    } glyphs[MAX_GLYPHS];
    int glyphCount;

    /* Visual index: sorted by screen X for cursor placement. */
    struct {
        int x;
        int advanceX;
        int byteStart;
        int byteEnd;
        int isRTL;
    } visualIndex[MAX_GLYPHS];
    int indexCount;

    int totalAdvance;            /* Total pixel width of the shaped string.   */

    /* Cluster break byte offsets for line fitting. */
    int clusterBreaks[MAX_CLUSTER_BREAKS];
    int clusterBreakCount;
} ShapedGlyphBuffer;

/*
 * WaylandShaper
 *
 *   Persistent per-font shaping state.
 */
typedef struct WaylandShaper {
    hb_buffer_t *buffer;        /* Reused HarfBuzz buffer.                  */

    /* Direct-mapped character → face cache (64 slots). */
    struct {
        FcChar32 uc;
        int      faceIdx;
    } charCache[64];

    /* Multi-entry string result cache (round-robin). */
    struct {
        char              text[MAX_STRING_CACHE];
        int               len;
        ShapedGlyphBuffer buffer;
        int               valid;
    } cache[CACHE_SLOTS];
    int cacheNext;

    int shapeErrors;
} WaylandShaper;

/*
 * WaylandFont
 *
 *   Main platform font structure.  TkFont MUST be first.
 */
typedef struct WaylandFont {
    TkFont          font;           /* Generic Tk font data — MUST be first. */

    /* Fontconfig multi-face array. */
    WaylandFtFace  *faces;
    int             nfaces;
    FcFontSet      *fontset;        /* Owned; destroyed in DeleteFont.       */
    FcPattern      *pattern;        /* Request pattern (owned).              */

    /* Convenience: nvgFontId of faces[0], set by EnsureNvgFont. */
    int             nvgFontId;

    /* Metrics (from the primary face via stbtt). */
    int             pixelSize;
    int             underlinePos;
    int             barHeight;

    /* Shaper. */
    WaylandShaper   shaper;
} WaylandFont;

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

typedef struct TkWaylandContext {
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
} TkWaylandContext;

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
    TkWaylandContext context;
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
    TkMenu *menubarMenuPtr;

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
    void		*winPtr;    /* TkWindow pointer. */
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
    NVGLUframebuffer *fb;
    GLFWwindow *glfwWindow;  /* The window whose GL context has the fbo.   */
    int width;               /* It is simpler to cache the fb dimensions.  */
    int height;
} TkWaylandPixmap;


TkWaylandPixmap* TkWaylandPixmapFromPixmap(Pixmap pixmap);
/*
 *----------------------------------------------------------------------
 *
 * The TkWindow structure contains a pointer to a struct TkWindowPrivate for
 * storing information specific to a port of Tk.  We use it for GLFW and
 * OpenGL objects associated to the window and for storing a string
 * for TkpGetString.
 *
 *----------------------------------------------------------------------
 */

typedef struct TkWindowPrivate {
    GLFWwindow          *glfwWindow;
    NVGLUframebuffer *fb;;  /* Backing store FBO structure pointer */
    Tcl_DString          pendingText;
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
#define RESIZE_RIGHT    (1 << 2)
#define RESIZE_TOP      (1 << 1)
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

MODULE_SCOPE int  TkWaylandInitialize(void);
MODULE_SCOPE void TkWaylandShutdown(ClientData clientData);

/*
 *----------------------------------------------------------------------
 *
 * Window Management
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE GLFWwindow *TkWaylandGetGLFWwindow(TkWindow *winPtr);

MODULE_SCOPE GLFWwindow *TkWaylandCreateWindow(
    TkWindow   *tkWin,
    int         width,
    int         height,
    const char *title,
    Drawable   *drawableOut);


MODULE_SCOPE void        TkWaylandDestroyWindow(GLFWwindow *glfwWindow);
MODULE_SCOPE Drawable    TkWaylandGetDrawable(GLFWwindow *w);
MODULE_SCOPE TkWindow*   TkWaylandGetTkWindow(GLFWwindow *glfwWindow);
MODULE_SCOPE GLFWwindow* TkWaylandGetGLFWwindowFromDrawable(Drawable drawable);
MODULE_SCOPE void        TkWaylandUpdateWindowSize(GLFWwindow *glfwWindow,
						int width, int height);
MODULE_SCOPE void        TkWaylandResizeWindow(GLFWwindow *w,
					    int width, int height);


/*
 *----------------------------------------------------------------------
 *
 * Drawing Context Management
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE int         TkWaylandBeginDraw(Drawable drawable, GC gc, TkWaylandDrawingContext *dcPtr);
MODULE_SCOPE void        TkWaylandEndDraw(TkWaylandDrawingContext *dcPtr);
MODULE_SCOPE NVGcontext *TkWaylandGetNVGContext(Drawable drawable);
MODULE_SCOPE NVGcontext *TkWaylandGetNVGContextForMeasure(void);

/*
 *----------------------------------------------------------------------
 *
 * GC Internals
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE GC   TkWaylandCreateGC(unsigned long valuemask, XGCValues *values);
MODULE_SCOPE void TkWaylandFreeGC(GC gc);
MODULE_SCOPE bool  TkWaylandGetGCValues(GC gc, unsigned long valuemask, XGCValues *values);
MODULE_SCOPE bool  TkWaylandChangeGC(GC gc, unsigned long valuemask, XGCValues *values);
MODULE_SCOPE bool  TkWaylandCopyGC(GC src, unsigned long valuemask, GC dst);

/*
 *----------------------------------------------------------------------
 *
 * Event Processing
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void TkWaylandProcessEvents(void);
MODULE_SCOPE void TkWaylandSetupCallbacks(GLFWwindow *glfwWindow);
MODULE_SCOPE void TkWaylandClearCallbacks(GLFWwindow *glfwWindow);
MODULE_SCOPE void Tk_WaylandSetupTkNotifier(void);
MODULE_SCOPE void TkWaylandQueueExposeEvent(TkWindow *winPtr, int x, int y,
					    int width, int height);
MODULE_SCOPE void TkWaylandWakeupGLFW(void);
MODULE_SCOPE void TkWaylandDisplayAllWindows(void);
MODULE_SCOPE KeySym TkWaylandGetKeysymFromScancode(int scancode);
void TkWaylandIbusSetCursorLocation(Tk_Window tkwin, int x, int y, int w, int h);

/*
 * Hit-testing and pointer-serial support for popup/menu dispatch.
 * Implemented in tkWaylandInit.c.
 */
MODULE_SCOPE TkWindow  *TkWaylandWindowAtPos(GLFWwindow *glfwWindow,
					     int x, int y);
MODULE_SCOPE void        TkWaylandRegisterPointerListener(void);

/*
 * Post a virtual event (e.g. "<<MenuDone>>") to a Tk window's event
 * queue.  Used to defer Tk-level cleanup (menu unposting, etc.) from
 * Wayland protocol callbacks to the normal Tcl event loop.  Implemented
 * in tkWaylandInit.c.
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

/* Variable for IME. */
extern int ibus_fd;
extern sd_bus *ibus_bus;
/*
 *----------------------------------------------------------------------
 *
 * Font Functions (from tkWaylandFont.c)
 *
 *----------------------------------------------------------------------
 */

/* Ensure all faces of a font are loaded into NanoVG and return primary ID. */
MODULE_SCOPE int EnsureNvgFont(WaylandFont *fontPtr, NVGcontext *vg);

/* Shape a string into a ShapedGlyphBuffer with full HarfBuzz + Bidi support. */
MODULE_SCOPE bool WaylandShaper_ShapeString(
    WaylandShaper     *shaper,
    WaylandFont       *fontPtr,
    const char        *source,
    int                numBytes,
    ShapedGlyphBuffer *buffer
);

/* Initialize a shaper structure. */
MODULE_SCOPE void WaylandShaper_Init(WaylandShaper *s);

/* Destroy a shaper structure and free resources. */
MODULE_SCOPE void WaylandShaper_Destroy(WaylandShaper *s);

/* Get the pixel size of a WaylandFont. */
MODULE_SCOPE int TkpGetFontPixelSize(Tk_Font tkfont);

/*
 *----------------------------------------------------------------------
 *
 * Color Conversion Utilities
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE NVGcolor TkWaylandXColorToNVG(XColor *xcolor);
MODULE_SCOPE NVGcolor TkWaylandPixelToNVG(unsigned long pixel);
MODULE_SCOPE void     TkWaylandApplyGC(NVGcontext *vg, GC gc);

/*
 *----------------------------------------------------------------------
 *
 * Keyboard Handling
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE bool  TkWaylandKeyInit();
MODULE_SCOPE void  TkWaylandKeyCleanup();
MODULE_SCOPE void  TkWaylandUpdateKeyboardModifiers(int glfw_mods);
MODULE_SCOPE void  TkWaylandStoreText(TkWindow *winPtr, unsigned int codepoint);
char* TkWaylandGetStoredText(TkWindow *winPtr);
void TkWaylandSetStoredText(TkWindow *winPtr, const char *text);
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

MODULE_SCOPE void TkWaylandErrorCallback(int error, const char *description);

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
void TkWaylandMenuInit(void);

#endif /* _TkWaylandINT_H */

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
