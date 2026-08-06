/*
 * tkWaylandInt.h --
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
#include "tkFont.h"
#include "tkIntPlatDecls.h"
#include "tkWaylandDefaults.h"

#include <GLFW/glfw3.h>
#include <GLES3/gl3.h>
#include <libdecor.h>
#include <xkbcommon/xkbcommon.h>

#include "nanovg.h"
#include "nanovg_gl_utils.h"

#include <fontconfig/fontconfig.h>
#include <hb.h>
#include <SheenBidi/SheenBidi.h>

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
 * Per-NVG-context font registration.
 * NanoVG font IDs are context-local.
 */
typedef struct NvgFontContext {
    NVGcontext *vg;
    int fontId;
    struct NvgFontContext *next;
} NvgFontContext;


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
    
    /* Fonts are stored per context. */
    NvgFontContext *nvgContexts;
    int             nvgContextCount;
} WaylandFont;

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
#define TKWL_NEEDS_DISPLAY  1
#define TKWL_DONT_SWAP      2
#define TKWL_NEVER_FOCUSED  4
#define TKWL_IS_DRAWING     8

typedef struct glfwTkInfo {
    GLFWwindow *glfwWindow;
    TkWindow *winPtr;
    NVGcontext *vg;
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
    NVGLUframebuffer *fb;       /* NULL for depth 1 pixmaps */
    GLFWwindow *glfwWindow;     /* The window whose GL context has the fb.*/
    unsigned int *rgba;         /* Possibly NULL RGBA data for this pixmap.
				 * Not NULL for depth 1 pixmaps. */
    int depth;
    int width;
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
typedef struct {
    float x, y, w, h;
} clipRect;

typedef struct TkWindowPrivate {
    GLFWwindow *glfwWindow;
    NVGLUframebuffer *fb;
    Tcl_DString pendingText;
    // Support for subwindow clipping
    clipRect *clipRectBuffer;
    int clipRectBufferSize;
    int clipRectCount;
    GLuint clipVAO;
    GLuint clipVBO;
    GLuint clipShader;
    GLint fbSizeUniform;
    clipRect containerRect;
    clipRect boundsRect;
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
MODULE_SCOPE NVGcontext* TkWaylandGetNVGContext(Drawable drawable);
MODULE_SCOPE NVGcontext* TkWaylandGetNVGContextForMeasure(void);
MODULE_SCOPE void        createClipShaders(TkWindow *winPtr);

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

MODULE_SCOPE void TkWaylandProcessEvents(void);
MODULE_SCOPE void TkWaylandSetupCallbacks(GLFWwindow *glfwWindow);
MODULE_SCOPE void TkWaylandClearCallbacks(GLFWwindow *glfwWindow);
MODULE_SCOPE void Tk_WaylandSetupTkNotifier(void);
MODULE_SCOPE void TkWaylandQueueExposeEvent(TkWindow *winPtr, int x, int y,
					    int width, int height);
MODULE_SCOPE void TkWaylandWakeupGLFW(void);
MODULE_SCOPE void TkWaylandDisplayAllWindows(void);
MODULE_SCOPE KeySym TkWaylandGetKeysymFromScancode(int scancode);


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

MODULE_SCOPE int   TkWaylandKeyInit();
MODULE_SCOPE void  TkWaylandKeyCleanup();
MODULE_SCOPE void  TkWaylandUpdateKeyboardModifiers(int glfw_mods);
MODULE_SCOPE void  TkWaylandStoreText(TkWindow *winPtr, unsigned int codepoint);
MODULE_SCOPE char* TkWaylandGetStoredText(TkWindow *winPtr);
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

/*
 *----------------------------------------------------------------------
 *
 * Support for clipping subwindows.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void tkWaylandDrawClipMask(TkWindow* winPtr,
					GLFWwindow* glfwWindow);

/*
 *----------------------------------------------------------------------
 *
 * Stub functions which are not declared elsewhere. (???? Why not?)
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void TkpSetCursor(Cursor cursor);

/*
 *----------------------------------------------------------------------
 *
 * Upper bound for the size of the feathering halo around an anti-aliased
 * shape.  NanoVG adds a 0.5 pixel halo around the floating point shape and
 * then adjusts the color of all pixels whose center is contained in the
 * expanded region.  But that can include pixels which are surprisingly
 * far from the floating point shape, especially when there is a very
 * sharp mitered join in a stroked path.  NanoVG does replace the miter
 * by a bevel if it is too sharp, but nonethless the bound is larger
 * than one would expect. 
 *
 *----------------------------------------------------------------------
 */

#define AA_PAD 6

#endif /* _TKGLFWINT_H */

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
