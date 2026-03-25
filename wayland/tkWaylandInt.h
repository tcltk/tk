/*
 * tkWaylandInt.h --
 *
 *	Internal definitions for the Wayland/GLFW/libcg backend.
 *	This file contains the private structures and function prototypes
 *	used by the Wayland implementation of Tk.
 *
 * Copyright © 1995-1997 Sun Microsystems, Inc.
 * Copyright © 2001-2009 Apple Inc.
 * Copyright © 2005-2009 Daniel A. Steffen <das@users.sourceforge.net>
 * Copyright © 2014 Marc Culler.
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef _TKWAYLANDINT
#define _TKWAYLANDINT

#include "tkInt.h"
#include <GLFW/glfw3.h>
#include <cg.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <stdint.h>

/*
 *----------------------------------------------------------------------
 *
 * Constants and magic numbers
 *
 *----------------------------------------------------------------------
 */

#define TK_WAYLAND_PIXMAP_MAGIC 0x574C504D	/* "WLPM" - identifies a pixmap */
#define TK_WAYLAND_GC_MAGIC     0x574C4743	/* "WLGC" - identifies a GC */

/*
 *----------------------------------------------------------------------
 *
 * Forward declarations
 *
 *----------------------------------------------------------------------
 */

typedef struct WindowMapping WindowMapping;
typedef struct TkWaylandGCImpl TkWaylandGCImpl;
typedef struct TkWaylandPixmapImpl TkWaylandPixmapImpl;
typedef struct DrawableMapping DrawableMapping;
typedef struct TextureState TextureState;
typedef struct TkWaylandDrawingContext TkWaylandDrawingContext;
typedef struct TkGlfwContext TkGlfwContext;

/*
 *----------------------------------------------------------------------
 *
 * TextureState --
 *
 *	OpenGL texture management for each window. This structure
 *	holds the OpenGL objects needed to display a libcg surface
 *	as a texture on the GPU.
 *
 *----------------------------------------------------------------------
 */

struct TextureState {
    GLuint texture_id;		/* OpenGL texture ID */
    GLuint vao;			/* Vertex Array Object (for OpenGL ES) */
    GLuint vbo;			/* Vertex Buffer Object */
    GLuint ibo;			/* Index Buffer Object */
    GLuint program;		/* Shader program for texture rendering */
    int width;			/* Current texture width */
    int height;			/* Current texture height */
    int needs_texture_update;	/* Flag indicating texture needs update */
};

/*
 *----------------------------------------------------------------------
 *
 * WindowMapping --
 *
 *	Associates a Tk window with a GLFW window and libcg surface.
 *	This is the core structure that ties together all three
 *	components of the display system.
 *
 *----------------------------------------------------------------------
 */

struct WindowMapping {
    TkWindow *tkWindow;		/* Tk window (may be NULL for pixmaps) */
    GLFWwindow *glfwWindow;	/* Native GLFW window handle */
    Drawable drawable;		/* X11-style drawable ID */
    int width;			/* Cached window width */
    int height;			/* Cached window height */
    int clearPending;		/* Flag indicating pending clear */
    int needsDisplay;		/* Flag indicating needs redisplay */
    int frameOpen;		/* Flag indicating frame is open */
    int inEventCycle;		/* Flag indicating in event cycle */
    struct cg_surface_t *surface;	/* libcg drawing surface */
    TextureState texture;	/* OpenGL texture state */
    WindowMapping *nextPtr;	/* Next in linked list */
};

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandGCImpl --
 *
 *	Implementation of a graphics context. This structure holds
 *	all the drawing attributes that would be stored in an X11 GC.
 *
 *----------------------------------------------------------------------
 */

struct TkWaylandGCImpl {
    unsigned int magic;		/* Magic number for validation */
    unsigned long foreground;	/* Foreground color (RGB) */
    unsigned long background;	/* Background color (RGB) */
    int line_width;		/* Line width in pixels */
    int line_style;		/* LineSolid, LineOnOffDash, LineDoubleDash */
    int cap_style;		/* CapButt, CapRound, CapProjecting */
    int join_style;		/* JoinMiter, JoinRound, JoinBevel */
    int fill_rule;		/* EvenOddRule, WindingRule */
    int arc_mode;		/* ArcChord, ArcPieSlice */
    void *font;			/* Font handle (unused, kept for compatibility) */
};

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandPixmapImpl --
 *
 *	Implementation of a pixmap (offscreen drawable). Pixmaps are
 *	software buffers that libcg renders into, independent of any
 *	window.
 *
 *----------------------------------------------------------------------
 */

struct TkWaylandPixmapImpl {
    unsigned int magic;		/* Magic number for validation */
    int type;			/* 1 for pixmap, 0 for invalid */
    int width;			/* Pixmap width */
    int height;			/* Pixmap height */
    Drawable drawable;		/* X11-style drawable ID */
    WindowMapping *windowMapping;	/* Associated window mapping (if any) */
    int frameOpen;		/* Flag indicating frame is open */
    struct cg_surface_t *surface;	/* libcg drawing surface */
    struct cg_ctx_t *cg;	/* libcg drawing context */
};

/*
 *----------------------------------------------------------------------
 *
 * DrawableMapping --
 *
 *	Associates a drawable with a window mapping. This allows
 *	fast lookup of the WindowMapping for any Drawable ID.
 *
 *----------------------------------------------------------------------
 */

struct DrawableMapping {
    Drawable drawable;		/* Drawable ID */
    WindowMapping *mapping;	/* Associated window mapping */
    DrawableMapping *next;	/* Next in linked list */
};

/*
 *----------------------------------------------------------------------
 *
 * TkGlfwContext --
 *
 *	Global GLFW/libcg context state. This holds the shared
 *	context window and global flags.
 *
 *----------------------------------------------------------------------
 */

struct TkGlfwContext {
    int initialized;		/* Flag indicating initialization */
    GLFWwindow *mainWindow;	/* Shared context window */
    struct cg_ctx_t *cg;	/* Global libcg context (may be NULL) */
    WindowMapping *activeFrame;	/* Currently active frame */
};

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandDrawingContext --
 *
 *	Context for a drawing operation. This holds the libcg context
 *	and associated state for a single draw operation.
 *
 *----------------------------------------------------------------------
 */

struct TkWaylandDrawingContext {
    struct cg_ctx_t *cg;	/* libcg drawing context */
    Drawable drawable;		/* Target drawable */
    GLFWwindow *glfwWindow;	/* Associated GLFW window */
    int width;			/* Drawable width */
    int height;			/* Drawable height */
    int offsetX;		/* X offset for child windows */
    int offsetY;		/* Y offset for child windows */
    int nestedFrame;		/* Flag for nested drawing */
};

/*
 *----------------------------------------------------------------------
 *
 * Global variables (declared in tkWaylandInit.c)
 *
 *----------------------------------------------------------------------
 */

extern struct TkGlfwContext glfwContext;
extern WindowMapping *windowMappingList;

/*
 *----------------------------------------------------------------------
 *
 * Function prototypes - Initialization and cleanup
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE int TkGlfwInitialize(void);
MODULE_SCOPE void TkGlfwShutdown(void *clientData);

/*
 *----------------------------------------------------------------------
 *
 * Function prototypes - Window management
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE GLFWwindow *TkGlfwCreateWindow(TkWindow *tkWin, int width,
					    int height, const char *title,
					    Drawable *drawableOut);
MODULE_SCOPE void TkGlfwDestroyWindow(GLFWwindow *glfwWindow);
MODULE_SCOPE void TkGlfwResizeWindow(GLFWwindow *w, int width, int height);
MODULE_SCOPE void TkGlfwUpdateWindowSize(GLFWwindow *glfwWindow,
					 int width, int height);
MODULE_SCOPE GLFWwindow *TkGlfwGetGLFWWindow(Tk_Window tkwin);
MODULE_SCOPE Drawable TkGlfwGetDrawable(GLFWwindow *w);
MODULE_SCOPE GLFWwindow *TkGlfwGetWindowFromDrawable(Drawable drawable);
MODULE_SCOPE TkWindow *TkGlfwGetTkWindow(GLFWwindow *glfwWindow);

/*
 *----------------------------------------------------------------------
 *
 * Function prototypes - Drawing operations
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE int TkGlfwBeginDraw(Drawable drawable, GC gc,
				 TkWaylandDrawingContext *dcPtr);
MODULE_SCOPE void TkGlfwEndDraw(TkWaylandDrawingContext *dcPtr);
MODULE_SCOPE void TkGlfwApplyGC(struct cg_ctx_t *cg, GC gc);
MODULE_SCOPE struct cg_ctx_t *TkGlfwGetCGContext(void);
MODULE_SCOPE struct cg_ctx_t *TkGlfwGetCGContextForMeasure(void);

/*
 *----------------------------------------------------------------------
 *
 * Function prototypes - Color conversion
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE struct cg_color_t TkGlfwXColorToCG(XColor *xcolor);
MODULE_SCOPE struct cg_color_t TkGlfwPixelToCG(unsigned long pixel);

/*
 *----------------------------------------------------------------------
 *
 * Function prototypes - OpenGL texture management
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE int TkGlfwInitializeTexture(WindowMapping *m);
MODULE_SCOPE void TkGlfwUploadSurfaceToTexture(WindowMapping *m);
MODULE_SCOPE void TkGlfwRenderTexture(WindowMapping *m);
MODULE_SCOPE void TkGlfwCleanupTexture(WindowMapping *m);

/*
 *----------------------------------------------------------------------
 *
 * Function prototypes - Mapping management
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE WindowMapping *FindMappingByGLFW(GLFWwindow *w);
MODULE_SCOPE WindowMapping *FindMappingByTk(TkWindow *w);
MODULE_SCOPE WindowMapping *FindMappingByDrawable(Drawable d);
MODULE_SCOPE WindowMapping *TkGlfwGetMappingList(void);
MODULE_SCOPE void AddMapping(WindowMapping *m);
MODULE_SCOPE void RemoveMapping(WindowMapping *m);
MODULE_SCOPE void CleanupAllMappings(void);
MODULE_SCOPE void RegisterDrawableForMapping(Drawable d, WindowMapping *m);
MODULE_SCOPE void SyncWindowSize(WindowMapping *m);

/*
 *----------------------------------------------------------------------
 *
 * Function prototypes - Event and display handling
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void Tk_WaylandSetupTkNotifier(void);
MODULE_SCOPE void TkWaylandWakeupGLFW(void);
MODULE_SCOPE void TkWaylandQueueExposeEvent(TkWindow *winPtr, int x, int y,
					    int width, int height);
MODULE_SCOPE void TkWaylandBeginEventCycle(WindowMapping *m);
MODULE_SCOPE void TkWaylandEndEventCycle(WindowMapping *m);
MODULE_SCOPE void TkWaylandScheduleDisplay(WindowMapping *m);
MODULE_SCOPE void TkWaylandDisplayProc(ClientData clientData);
MODULE_SCOPE void TkWaylandProcessEvents(void);

/*
 *----------------------------------------------------------------------
 *
 * Function prototypes - GC operations
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE GC TkWaylandCreateGC(unsigned long valuemask, XGCValues *values);
MODULE_SCOPE void TkWaylandFreeGC(GC gc);
MODULE_SCOPE int TkWaylandGetGCValues(GC gc, unsigned long valuemask,
				      XGCValues *values);
MODULE_SCOPE int TkWaylandChangeGC(GC gc, unsigned long valuemask,
				   XGCValues *values);
MODULE_SCOPE int TkWaylandCopyGC(GC src, unsigned long valuemask, GC dst);

/*
 *----------------------------------------------------------------------
 *
 * Function prototypes - Miscellaneous
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void TkWaylandAccessibility_Init(Tcl_Interp *interp);
MODULE_SCOPE void TkWaylandMenuInit(void);
MODULE_SCOPE void TkGlfwSetupCallbacks(GLFWwindow *window, TkWindow *tkWin);

#endif /* _TKWAYLANDINT */
