/*
 * tkWaylandDraw.c --
 *
 *	This file contains functions that draw to windows using Wayland,
 *	GLFW, and NanoVG. Many of these functions emulate Xlib functions.
 *
 * Copyright © 1995-1997 Sun Microsystems, Inc.
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkPort.h"
#include "tkButton.h"
#include "nanovg.h"

/* GLFW Integration. */
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WAYLAND
#include <GLFW/glfw3native.h>
#include "tkGlfwInt.h"

/* OpenGL for NanoVG. */
#include <GL/gl.h>

#define radians(d)	((d) * (M_PI/180.0))


/*
 * Global state.
 */
static NVGcontext* globalVGContext = NULL;
static GLFWwindow* globalGLFWWindow = NULL;
static DrawableMap* drawableMap = NULL;
static int glfwInitialized = 0;

#ifndef _TKWAYLANDDRAW_H
#define _TKWAYLANDDRAW_H

#include "tkInt.h"
#include <GLFW/glfw3.h>

/*
 * Forward declarations
 */
typedef struct WaylandDrawable WaylandDrawable;
typedef struct TkWaylandDrawingContext TkWaylandDrawingContext;

/*
 * Initialization and cleanup.
 */
MODULE_SCOPE int TkWaylandInitDrawing(Tcl_Interp *interp, 
                                       struct wl_display* display);
MODULE_SCOPE void TkWaylandCleanupDrawing(void);

/*
 * Window/Drawable management.
 */
MODULE_SCOPE WaylandDrawable* TkWaylandCreateDrawable(Drawable tkDrawable,
                                                       int width,
                                                       int height,
                                                       const char* title);
MODULE_SCOPE GLFWwindow* Tk_WaylandGetGLFWWindow(Drawable drawable);
MODULE_SCOPE void* Tk_WaylandGetContextForDrawable(Drawable drawable);

/*
 * Drawing context management.
 */
MODULE_SCOPE bool TkWaylandSetupDrawingContext(Drawable d, GC gc,
                                                TkWaylandDrawingContext *dcPtr);
MODULE_SCOPE void TkWaylandRestoreDrawingContext(TkWaylandDrawingContext *dcPtr);

/*
 * Event handling.
 */
MODULE_SCOPE void TkWaylandPollEvents(void);

/*
 * Xlib emulation functions. 
 */
int XDrawLines(Display *display, Drawable d, GC gc, XPoint *points,
               int npoints, int mode);
int XDrawSegments(Display *display, Drawable d, GC gc, XSegment *segments,
                  int nsegments);
int XFillPolygon(Display *display, Drawable d, GC gc, XPoint *points,
                 int npoints, int shape, int mode);
int XDrawRectangle(Display *display, Drawable d, GC gc, int x, int y,
                   unsigned int width, unsigned int height);
int XDrawRectangles(Display *display, Drawable d, GC gc, XRectangle *rectArr,
                    int nRects);
int XFillRectangles(Display *display, Drawable d, GC gc, XRectangle *rectangles,
                    int n_rectangles);
int XDrawArc(Display *display, Drawable d, GC gc, int x, int y,
             unsigned int width, unsigned int height, int angle1, int angle2);
int XDrawArcs(Display *display, Drawable d, GC gc, XArc *arcArr, int nArcs);
int XFillArc(Display *display, Drawable d, GC gc, int x, int y,
             unsigned int width, unsigned int height, int angle1, int angle2);
int XFillArcs(Display *display, Drawable d, GC gc, XArc *arcArr, int nArcs);

/*
 * Tk-specific drawing functions.
 */
void Tk_DrawHighlightBorder(Tk_Window tkwin, GC fgGC, GC bgGC,
                            int highlightWidth, Drawable drawable);
void TkpDrawFrameEx(Tk_Window tkwin, Drawable drawable, Tk_3DBorder border,
                    int highlightWidth, int borderWidth, int relief);

#endif /* _TKWAYLANDDRAW_H */

/*
 * Prototypes for functions used only in this file.
 */
static void ClipToGC(Drawable d, GC gc, int* clip_x, int* clip_y,
                     int* clip_width, int* clip_height);
static NVGcontext* GetNVGContextForDrawable(Drawable drawable);
static WaylandDrawable* GetWaylandDrawable(Drawable drawable);
static void RegisterDrawable(Drawable tkDrawable, WaylandDrawable* waylandDrawable);
static void UnregisterDrawable(Drawable tkDrawable);
static void GLFWErrorCallback(int error, const char* description);
static void GLFWFramebufferSizeCallback(GLFWwindow* window, int width, int height);

/*
 *----------------------------------------------------------------------
 *
 * GLFWErrorCallback --
 *
 *	GLFW error callback for debugging.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Prints error to stderr.
 *
 *----------------------------------------------------------------------
 */

static void
GLFWErrorCallback(
    int error,
    const char* description)
{
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

/*
 *----------------------------------------------------------------------
 *
 * GLFWFramebufferSizeCallback --
 *
 *	Handle window resize events.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates drawable size and viewport.
 *
 *----------------------------------------------------------------------
 */

static void
GLFWFramebufferSizeCallback(
    GLFWwindow* window,
    int width,
    int height)
{
    WaylandDrawable* wd = (WaylandDrawable*)glfwGetWindowUserPointer(window);
    if (wd) {
        wd->width = width;
        wd->height = height;
        glViewport(0, 0, width, height);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandInitDrawing --
 *
 *	Initializes Wayland, GLFW, and NanoVG drawing.
 *
 * Results:
 *	TCL_OK on success, TCL_ERROR on failure.
 *
 * Side effects:
 *	Initializes GLFW and NanoVG context.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE int
TkWaylandInitDrawing(
    Tcl_Interp *interp,
    struct wl_display* display)
{
    /* Initialize GLFW. */
    if (!glfwInitialized) {
        glfwSetErrorCallback(GLFWErrorCallback);
        
        if (!glfwInit()) {
            Tcl_SetResult(interp, "Failed to initialize GLFW", TCL_STATIC);
            return TCL_ERROR;
        }
        glfwInitialized = 1;
        
        /* Configure GLFW for Wayland. */
        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
        glfwWindowHint(GLFW_SAMPLES, 4); /* 4x MSAA */
        
        /* Create a hidden global window for context sharing. */
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        globalGLFWWindow = glfwCreateWindow(1, 1, "Tk Global Context", NULL, NULL);
        if (!globalGLFWWindow) {
            glfwTerminate();
            glfwInitialized = 0;
            Tcl_SetResult(interp, "Failed to create GLFW window", TCL_STATIC);
            return TCL_ERROR;
        }
        
        glfwMakeContextCurrent(globalGLFWWindow);
        
        /* Enable VSync. */
        glfwSwapInterval(1);
    }
    
    /* Initialize NanoVG context */
    if (!globalVGContext) {
        glfwMakeContextCurrent(globalGLFWWindow);
        
        /* Create NanoVG context with OpenGL 3 backend. */
        globalVGContext = nvgCreateGL3(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
        if (!globalVGContext) {
            Tcl_SetResult(interp, "Failed to create NanoVG context", TCL_STATIC);
            return TCL_ERROR;
        }
    }
    
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandCleanupDrawing --
 *
 *	Cleans up Wayland, GLFW, and NanoVG resources.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Destroys NanoVG context and terminates GLFW.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandCleanupDrawing(void)
{
    DrawableMap* current = drawableMap;
    DrawableMap* next;
    
    /* Clean up all registered drawables. */
    while (current) {
        next = current->next;
        if (current->waylandDrawable) {
            if (current->waylandDrawable->glfwWindow) {
                glfwDestroyWindow(current->waylandDrawable->glfwWindow);
            }
            ckfree((char*)current->waylandDrawable);
        }
        ckfree((char*)current);
        current = next;
    }
    drawableMap = NULL;
    
    /* Clean up NanoVG. */
    if (globalVGContext) {
        nvgDeleteGL3(globalVGContext);
        globalVGContext = NULL;
    }
    
    /* Clean up GLFW. */
    if (globalGLFWWindow) {
        glfwDestroyWindow(globalGLFWWindow);
        globalGLFWWindow = NULL;
    }
    
    if (glfwInitialized) {
        glfwTerminate();
        glfwInitialized = 0;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandCreateDrawable --
 *
 *	Creates a new Wayland drawable with GLFW window.
 *
 * Results:
 *	Pointer to WaylandDrawable or NULL on failure.
 *
 * Side effects:
 *	Creates GLFW window and maps it to Tk drawable.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE WaylandDrawable*
TkWaylandCreateDrawable(
    Drawable tkDrawable,
    int width,
    int height,
    const char* title)
{
    WaylandDrawable* wd;
    GLFWwindow* window;
    
    if (!glfwInitialized || !globalVGContext) {
        return NULL;
    }
    
    /* Configure window hints */
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    
    /* Create GLFW window sharing context with global window */
    window = glfwCreateWindow(width, height, title ? title : "Tk Window",
                              NULL, globalGLFWWindow);
    if (!window) {
        return NULL;
    }
    
    /* Allocate drawable structure */
    wd = (WaylandDrawable*)ckalloc(sizeof(WaylandDrawable));
    wd->glfwWindow = window;
    wd->vg = globalVGContext;
    wd->width = width;
    wd->height = height;
    wd->needsSwap = 0;
    
    /* Get native Wayland surface */
    wd->surface = glfwGetWaylandWindow(window);
    
    /* Set up callbacks */
    glfwSetWindowUserPointer(window, wd);
    glfwSetFramebufferSizeCallback(window, GLFWFramebufferSizeCallback);
    
    /* Make context current and set up viewport */
    glfwMakeContextCurrent(window);
    glViewport(0, 0, width, height);
    
    /* Register drawable mapping */
    RegisterDrawable(tkDrawable, wd);
    
    return wd;
}

/*
 *----------------------------------------------------------------------
 *
 * RegisterDrawable --
 *
 *	Registers mapping between Tk drawable and WaylandDrawable.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Adds entry to drawable map.
 *
 *----------------------------------------------------------------------
 */

static void
RegisterDrawable(
    Drawable tkDrawable,
    WaylandDrawable* waylandDrawable)
{
    DrawableMap* map = (DrawableMap*)ckalloc(sizeof(DrawableMap));
    map->tkDrawable = tkDrawable;
    map->waylandDrawable = waylandDrawable;
    map->next = drawableMap;
    drawableMap = map;
}

/*
 *----------------------------------------------------------------------
 *
 * UnregisterDrawable --
 *
 *	Removes drawable mapping.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Removes entry from drawable map.
 *
 *----------------------------------------------------------------------
 */

static void
UnregisterDrawable(
    Drawable tkDrawable)
{
    DrawableMap** mapPtr = &drawableMap;
    DrawableMap* current;
    
    while (*mapPtr) {
        current = *mapPtr;
        if (current->tkDrawable == tkDrawable) {
            *mapPtr = current->next;
            ckfree((char*)current);
            return;
        }
        mapPtr = &current->next;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * GetWaylandDrawable --
 *
 *	Gets WaylandDrawable structure from X Drawable.
 *
 * Results:
 *	Pointer to WaylandDrawable or NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static WaylandDrawable*
GetWaylandDrawable(
    Drawable drawable)
{
    DrawableMap* current = drawableMap;
    
    while (current) {
        if (current->tkDrawable == drawable) {
            return current->waylandDrawable;
        }
        current = current->next;
    }
    
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * GetNVGContextForDrawable --
 *
 *	Get NanoVG context for given Drawable.
 *
 * Results:
 *	NanoVG context or NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static NVGcontext*
GetNVGContextForDrawable(
    Drawable drawable)
{
    WaylandDrawable* wd = GetWaylandDrawable(drawable);
    if (wd && wd->vg) {
        return wd->vg;
    }
    return globalVGContext;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_WaylandGetContextForDrawable --
 *
 *	Get drawing context for given Drawable.
 *
 * Results:
 *	Drawing context.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void*
Tk_WaylandGetContextForDrawable(
    Drawable drawable)
{
    return GetNVGContextForDrawable(drawable);
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_WaylandGetGLFWWindow --
 *
 *	Get GLFW window for given Drawable.
 *
 * Results:
 *	GLFW window handle or NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE GLFWwindow*
Tk_WaylandGetGLFWWindow(
    Drawable drawable)
{
    WaylandDrawable* wd = GetWaylandDrawable(drawable);
    return wd ? wd->glfwWindow : NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandSetupDrawingContext --
 *
 *	Set up a drawing context for the given drawable from an X GC.
 *
 * Results:
 *	Boolean indicating whether it is ok to draw.
 *
 * Side effects:
 *	Sets up NanoVG state for drawing and makes GLFW context current.
 *
 *----------------------------------------------------------------------
 */

bool
TkWaylandSetupDrawingContext(
    Drawable d,
    GC gc,
    TkWaylandDrawingContext *dcPtr)
{
    NVGcontext* vg = GetNVGContextForDrawable(d);
    WaylandDrawable* wd = GetWaylandDrawable(d);
    
    if (!vg || !wd || !wd->glfwWindow) {
        return false;
    }
    
    /* Make GLFW context current */
    glfwMakeContextCurrent(wd->glfwWindow);
    
    /* Initialize drawing context */
    dcPtr->vg = vg;
    dcPtr->glfwWindow = wd->glfwWindow;
    dcPtr->clip_x = 0;
    dcPtr->clip_y = 0;
    dcPtr->clip_width = wd->width;
    dcPtr->clip_height = wd->height;
    dcPtr->frameActive = 0;
    
    /* Apply GC clipping */
    ClipToGC(d, gc, &dcPtr->clip_x, &dcPtr->clip_y,
             &dcPtr->clip_width, &dcPtr->clip_height);
    
    /* Begin NanoVG frame */
    nvgBeginFrame(vg, wd->width, wd->height, 1.0f);
    dcPtr->frameActive = 1;
    
    /* Set up clipping */
    if (dcPtr->clip_width > 0 && dcPtr->clip_height > 0) {
        nvgScissor(vg, dcPtr->clip_x, dcPtr->clip_y,
                   dcPtr->clip_width, dcPtr->clip_height);
    }
    
    /* Apply GC properties to NanoVG */
    if (gc) {
        /* Convert X color to NanoVG color */
        XColor xcolor;
        Tk_GetColorFromObj(NULL, (Tk_Uid)gc->foreground, &xcolor);
        NVGcolor color = nvgRGB(xcolor.red >> 8, xcolor.green >> 8,
                                xcolor.blue >> 8);
        
        nvgStrokeColor(vg, color);
        nvgFillColor(vg, color);
        nvgStrokeWidth(vg, gc->line_width > 0 ? gc->line_width : 1.0f);
        
        /* Set line style */
        switch (gc->line_style) {
            case LineSolid:
                nvgLineCap(vg, NVG_BUTT);
                break;
            case LineOnOffDash:
            case LineDoubleDash:
                /* TODO: Implement dash patterns using nvgDashPattern */
                break;
        }
        
        /* Set line cap */
        switch (gc->cap_style) {
            case CapNotLast:
            case CapButt:
                nvgLineCap(vg, NVG_BUTT);
                break;
            case CapRound:
                nvgLineCap(vg, NVG_ROUND);
                break;
            case CapProjecting:
                nvgLineCap(vg, NVG_SQUARE);
                break;
        }
        
        /* Set line join */
        switch (gc->join_style) {
            case JoinMiter:
                nvgLineJoin(vg, NVG_MITER);
                break;
            case JoinRound:
                nvgLineJoin(vg, NVG_ROUND);
                break;
            case JoinBevel:
                nvgLineJoin(vg, NVG_BEVEL);
                break;
        }
    }
    
    /* Mark that buffer swap will be needed */
    wd->needsSwap = 1;
    
    return true;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandRestoreDrawingContext --
 *
 *	Restore drawing context and swap buffers.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Ends NanoVG frame and swaps GLFW buffers.
 *
 *----------------------------------------------------------------------
 */

void
TkWaylandRestoreDrawingContext(
    TkWaylandDrawingContext *dcPtr)
{
    if (dcPtr->vg && dcPtr->frameActive) {
        nvgEndFrame(dcPtr->vg);
        dcPtr->frameActive = 0;
    }
    
    if (dcPtr->glfwWindow) {
        /* Swap buffers to display the rendered content */
        glfwSwapBuffers(dcPtr->glfwWindow);
        
        /* Poll events to keep window responsive */
        glfwPollEvents();
    }
    
    dcPtr->vg = NULL;
    dcPtr->glfwWindow = NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandPollEvents --
 *
 *	Poll GLFW events. Should be called periodically.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Processes pending window events.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandPollEvents(void)
{
    if (glfwInitialized) {
        glfwPollEvents();
    }
}

/*
 *----------------------------------------------------------------------
 *
 * XDrawLines --
 *
 *	Draw connected lines.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	Renders a series of connected lines.
 *
 *----------------------------------------------------------------------
 */

int
XDrawLines(
    Display *display,		/* Display. */
    Drawable d,			/* Draw on this. */
    GC gc,			/* Use this GC. */
    XPoint *points,		/* Array of points. */
    int npoints,		/* Number of points. */
    int mode)			/* Line drawing mode. */
{
    TkWaylandDrawingContext dc;
    WaylandDrawable* wd = GetWaylandDrawable(d);
    int i;
    
    if (npoints < 2 || !wd) {
        return BadValue;
    }
    
    LastKnownRequestProcessed(display)++;
    if (!TkWaylandSetupDrawingContext(d, gc, &dc)) {
        return BadDrawable;
    }
    
    if (dc.vg) {
        nvgBeginPath(dc.vg);
        
        if (mode == CoordModeOrigin) {
            nvgMoveTo(dc.vg, points[0].x, points[0].y);
            for (i = 1; i < npoints; i++) {
                nvgLineTo(dc.vg, points[i].x, points[i].y);
            }
        } else {
            /* Relative mode */
            float x = points[0].x;
            float y = points[0].y;
            nvgMoveTo(dc.vg, x, y);
            for (i = 1; i < npoints; i++) {
                x += points[i].x;
                y += points[i].y;
                nvgLineTo(dc.vg, x, y);
            }
        }
        
        nvgStroke(dc.vg);
    }
    
    TkWaylandRestoreDrawingContext(&dc);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XDrawSegments --
 *
 *	Draw unconnected lines.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	Renders a series of unconnected lines.
 *
 *----------------------------------------------------------------------
 */

int
XDrawSegments(
    Display *display,
    Drawable d,
    GC gc,
    XSegment *segments,
    int nsegments)
{
    TkWaylandDrawingContext dc;
    WaylandDrawable* wd = GetWaylandDrawable(d);
    int i;
    
    LastKnownRequestProcessed(display)++;
    if (!TkWaylandSetupDrawingContext(d, gc, &dc)) {
        return BadDrawable;
    }
    
    if (dc.vg) {
        for (i = 0; i < nsegments; i++) {
            nvgBeginPath(dc.vg);
            nvgMoveTo(dc.vg, segments[i].x1, segments[i].y1);
            nvgLineTo(dc.vg, segments[i].x2, segments[i].y2);
            nvgStroke(dc.vg);
        }
    }
    
    TkWaylandRestoreDrawingContext(&dc);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XFillPolygon --
 *
 *	Draws a filled polygon.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	Draws a filled polygon on the specified drawable.
 *
 *----------------------------------------------------------------------
 */

int
XFillPolygon(
    Display *display,		/* Display. */
    Drawable d,			/* Draw on this. */
    GC gc,			/* Use this GC. */
    XPoint *points,		/* Array of points. */
    int npoints,		/* Number of points. */
    TCL_UNUSED(int),		/* Shape to draw. */
    int mode)			/* Drawing mode. */
{
    TkWaylandDrawingContext dc;
    WaylandDrawable* wd = GetWaylandDrawable(d);
    int i;
    
    LastKnownRequestProcessed(display)++;
    if (!TkWaylandSetupDrawingContext(d, gc, &dc)) {
        return BadDrawable;
    }
    
    if (dc.vg) {
        nvgBeginPath(dc.vg);
        
        if (mode == CoordModeOrigin) {
            nvgMoveTo(dc.vg, points[0].x, points[0].y);
            for (i = 1; i < npoints; i++) {
                nvgLineTo(dc.vg, points[i].x, points[i].y);
            }
        } else {
            /* Relative mode */
            float x = points[0].x;
            float y = points[0].y;
            nvgMoveTo(dc.vg, x, y);
            for (i = 1; i < npoints; i++) {
                x += points[i].x;
                y += points[i].y;
                nvgLineTo(dc.vg, x, y);
            }
        }
        
        nvgClosePath(dc.vg);
        
        if (gc->fill_rule == EvenOddRule) {
            nvgPathWinding(dc.vg, NVG_HOLE);
        } else {
            nvgPathWinding(dc.vg, NVG_SOLID);
        }
        
        nvgFill(dc.vg);
    }
    
    TkWaylandRestoreDrawingContext(&dc);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XDrawRectangle --
 *
 *	Draws a rectangle.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	Draws a rectangle on the specified drawable.
 *
 *----------------------------------------------------------------------
 */

int
XDrawRectangle(
    Display *display,		/* Display. */
    Drawable d,			/* Draw on this. */
    GC gc,			/* Use this GC. */
    int x, int y,		/* Upper left corner. */
    unsigned int width,		/* Width & height of rect. */
    unsigned int height)
{
    TkWaylandDrawingContext dc;
    WaylandDrawable* wd = GetWaylandDrawable(d);
    
    if (width == 0 || height == 0 || !wd) {
        return BadDrawable;
    }
    
    LastKnownRequestProcessed(display)++;
    if (!TkWaylandSetupDrawingContext(d, gc, &dc)) {
        return BadDrawable;
    }
    
    if (dc.vg) {
        nvgBeginPath(dc.vg);
        nvgRect(dc.vg, x, y, width, height);
        nvgStroke(dc.vg);
    }
    
    TkWaylandRestoreDrawingContext(&dc);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XDrawRectangles --
 *
 *	Draws the outlines of the specified rectangles.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	Draws rectangles on the specified drawable.
 *
 *----------------------------------------------------------------------
 */

int
XDrawRectangles(
    Display *display,
    Drawable d,
    GC gc,
    XRectangle *rectArr,
    int nRects)
{
    TkWaylandDrawingContext dc;
    WaylandDrawable* wd = GetWaylandDrawable(d);
    int i;
    
    LastKnownRequestProcessed(display)++;
    if (!TkWaylandSetupDrawingContext(d, gc, &dc)) {
        return BadDrawable;
    }
    
    if (dc.vg) {
        for (i = 0; i < nRects; i++) {
            if (rectArr[i].width == 0 || rectArr[i].height == 0) {
                continue;
            }
            nvgBeginPath(dc.vg);
            nvgRect(dc.vg, rectArr[i].x, rectArr[i].y,
                    rectArr[i].width, rectArr[i].height);
            nvgStroke(dc.vg);
        }
    }
    
    TkWaylandRestoreDrawingContext(&dc);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XFillRectangles --
 *
 *	Fill multiple rectangular areas in the given drawable.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	Draws onto the specified drawable.
 *
 *----------------------------------------------------------------------
 */

int
XFillRectangles(
    Display *display,		/* Display. */
    Drawable d,			/* Draw on this. */
    GC gc,			/* Use this GC. */
    XRectangle *rectangles,	/* Rectangle array. */
    int n_rectangles)		/* Number of rectangles. */
{
    TkWaylandDrawingContext dc;
    WaylandDrawable* wd = GetWaylandDrawable(d);
    int i;
    
    LastKnownRequestProcessed(display)++;
    if (!TkWaylandSetupDrawingContext(d, gc, &dc)) {
        return BadDrawable;
    }
    
    if (dc.vg) {
        for (i = 0; i < n_rectangles; i++) {
            if (rectangles[i].width == 0 || rectangles[i].height == 0) {
                continue;
            }
            nvgBeginPath(dc.vg);
            nvgRect(dc.vg, rectangles[i].x, rectangles[i].y,
                    rectangles[i].width, rectangles[i].height);
            nvgFill(dc.vg);
        }
    }
    
    TkWaylandRestoreDrawingContext(&dc);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XDrawArc --
 *
 *	Draw an arc.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	Draws an arc on the specified drawable.
 *
 *----------------------------------------------------------------------
 */

int
XDrawArc(
    Display *display,		/* Display. */
    Drawable d,			/* Draw on this. */
    GC gc,			/* Use this GC. */
    int x, int y,		/* Upper left of bounding rect. */
    unsigned int width,		/* Width & height. */
    unsigned int height,
    int angle1,			/* Starting angle of arc. */
    int angle2)			/* Extent of arc. */
{
    TkWaylandDrawingContext dc;
    WaylandDrawable* wd = GetWaylandDrawable(d);
    
    if (width == 0 || height == 0 || angle2 == 0 || !wd) {
        return BadDrawable;
    }
    
    LastKnownRequestProcessed(display)++;
    if (!TkWaylandSetupDrawingContext(d, gc, &dc)) {
        return BadDrawable;
    }
    
    if (dc.vg) {
        float cx = x + width / 2.0f;
        float cy = y + height / 2.0f;
        float rx = width / 2.0f;
        float ry = height / 2.0f;
        
        /* Convert X angles (1/64 degree units, clockwise from 3 o'clock)
           to NanoVG angles (radians, clockwise from 3 o'clock) */
        float startAngle = -radians(angle1 / 64.0);
        float endAngle = -radians((angle1 + angle2) / 64.0);
        
        nvgBeginPath(dc.vg);
        
        if (width == height) {
            /* Circular arc */
            nvgArc(dc.vg, cx, cy, rx, startAngle, endAngle, NVG_CW);
        } else {
            /* Elliptical arc - NanoVG doesn't have direct elliptical arc,
               so we use a transformed circle */
            nvgSave(dc.vg);
            nvgTranslate(dc.vg, cx, cy);
            nvgScale(dc.vg, 1.0f, ry / rx);
            nvgTranslate(dc.vg, -cx, -cy);
            nvgArc(dc.vg, cx, cy, rx, startAngle, endAngle, NVG_CW);
            nvgRestore(dc.vg);
        }
        
        nvgStroke(dc.vg);
    }
    
    TkWaylandRestoreDrawingContext(&dc);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XDrawArcs --
 *
 *	Draws multiple circular or elliptical arcs.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	Draws an arc for each array element on the specified drawable.
 *
 *----------------------------------------------------------------------
 */

int
XDrawArcs(
    Display *display,
    Drawable d,
    GC gc,
    XArc *arcArr,
    int nArcs)
{
    TkWaylandDrawingContext dc;
    WaylandDrawable* wd = GetWaylandDrawable(d);
    int i;
    
    LastKnownRequestProcessed(display)++;
    if (!TkWaylandSetupDrawingContext(d, gc, &dc)) {
        return BadDrawable;
    }
    
    if (dc.vg) {
        for (i = 0; i < nArcs; i++) {
            if (arcArr[i].width == 0 || arcArr[i].height == 0 ||
                arcArr[i].angle2 == 0) {
                continue;
            }
            
            float cx = arcArr[i].x + arcArr[i].width / 2.0f;
            float cy = arcArr[i].y + arcArr[i].height / 2.0f;
            float rx = arcArr[i].width / 2.0f;
            float ry = arcArr[i].height / 2.0f;
            
            float startAngle = -radians(arcArr[i].angle1 / 64.0);
            float endAngle = -radians((arcArr[i].angle1 + arcArr[i].angle2) / 64.0);
            
            nvgBeginPath(dc.vg);
            
            if (arcArr[i].width == arcArr[i].height) {
                nvgArc(dc.vg, cx, cy, rx, startAngle, endAngle, NVG_CW);
            } else {
                nvgSave(dc.vg);
                nvgTranslate(dc.vg, cx, cy);
                nvgScale(dc.vg, 1.0f, ry / rx);
                nvgTranslate(dc.vg, -cx, -cy);
                nvgArc(dc.vg, cx, cy, rx, startAngle, endAngle, NVG_CW);
                nvgRestore(dc.vg);
            }
            
            nvgStroke(dc.vg);
        }
    }
    
    TkWaylandRestoreDrawingContext(&dc);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XFillArc --
 *
 *	Draw a filled arc.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	Draws a filled arc on the specified drawable.
 *
 *----------------------------------------------------------------------
 */

int
XFillArc(
    Display *display,		/* Display. */
    Drawable d,			/* Draw on this. */
    GC gc,			/* Use this GC. */
    int x, int y,		/* Upper left of bounding rect. */
    unsigned int width,		/* Width & height. */
    unsigned int height,
    int angle1,			/* Starting angle of arc. */
    int angle2)			/* Extent of arc. */
{
    TkWaylandDrawingContext dc;
    WaylandDrawable* wd = GetWaylandDrawable(d);
    
    if (width == 0 || height == 0 || angle2 == 0 || !wd) {
        return BadDrawable;
    }
    
    LastKnownRequestProcessed(display)++;
    if (!TkWaylandSetupDrawingContext(d, gc, &dc)) {
        return BadDrawable;
    }
    
    if (dc.vg) {
        float cx = x + width / 2.0f;
        float cy = y + height / 2.0f;
        float rx = width / 2.0f;
        float ry = height / 2.0f;
        
        float startAngle = -radians(angle1 / 64.0);
        float endAngle = -radians((angle1 + angle2) / 64.0);
        
        nvgBeginPath(dc.vg);
        
        if (gc->arc_mode == ArcPieSlice) {
            /* Pie slice: line from center to start, arc, line back to center */
            nvgMoveTo(dc.vg, cx, cy);
        }
        
        if (width == height) {
            nvgArc(dc.vg, cx, cy, rx, startAngle, endAngle, NVG_CW);
        } else {
            nvgSave(dc.vg);
            nvgTranslate(dc.vg, cx, cy);
            nvgScale(dc.vg, 1.0f, ry / rx);
            nvgTranslate(dc.vg, -cx, -cy);
            nvgArc(dc.vg, cx, cy, rx, startAngle, endAngle, NVG_CW);
            nvgRestore(dc.vg);
        }
        
        if (gc->arc_mode == ArcPieSlice) {
            nvgLineTo(dc.vg, cx, cy);
        }
        
        nvgClosePath(dc.vg);
        nvgFill(dc.vg);
    }
    
    TkWaylandRestoreDrawingContext(&dc);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XFillArcs --
 *
 *	Draw multiple filled arcs.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	Draws a filled arc for each array element on the specified drawable.
 *
 *----------------------------------------------------------------------
 */

int
XFillArcs(
    Display *display,
    Drawable d,
    GC gc,
    XArc *arcArr,
    int nArcs)
{
    TkWaylandDrawingContext dc;
    WaylandDrawable* wd = GetWaylandDrawable(d);
    int i;
    
    LastKnownRequestProcessed(display)++;
    if (!TkWaylandSetupDrawingContext(d, gc, &dc)) {
        return BadDrawable;
    }
    
    if (dc.vg) {
        for (i = 0; i < nArcs; i++) {
            if (arcArr[i].width == 0 || arcArr[i].height == 0 ||
                arcArr[i].angle2 == 0) {
                continue;
            }
            
            float cx = arcArr[i].x + arcArr[i].width / 2.0f;
            float cy = arcArr[i].y + arcArr[i].height / 2.0f;
            float rx = arcArr[i].width / 2.0f;
            float ry = arcArr[i].height / 2.0f;
            
            float startAngle = -radians(arcArr[i].angle1 / 64.0);
            float endAngle = -radians((arcArr[i].angle1 + arcArr[i].angle2) / 64.0);
            
            nvgBeginPath(dc.vg);
            
            if (gc->arc_mode == ArcPieSlice) {
                nvgMoveTo(dc.vg, cx, cy);
            }
            
            if (arcArr[i].width == arcArr[i].height) {
                nvgArc(dc.vg, cx, cy, rx, startAngle, endAngle, NVG_CW);
            } else {
                nvgSave(dc.vg);
                nvgTranslate(dc.vg, cx, cy);
                nvgScale(dc.vg, 1.0f, ry / rx);
                nvgTranslate(dc.vg, -cx, -cy);
                nvgArc(dc.vg, cx, cy, rx, startAngle, endAngle, NVG_CW);
                nvgRestore(dc.vg);
            }
            
            if (gc->arc_mode == ArcPieSlice) {
                nvgLineTo(dc.vg, cx, cy);
            }
            
            nvgClosePath(dc.vg);
            nvgFill(dc.vg);
        }
    }
    
    TkWaylandRestoreDrawingContext(&dc);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * ClipToGC --
 *
 *	Helper function to intersect given region with gc clip region.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Modifies clip parameters.
 *
 *----------------------------------------------------------------------
 */

static void
ClipToGC(
    Drawable d,
    GC gc,
    int* clip_x,
    int* clip_y,
    int* clip_width,
    int* clip_height)
{
    if (gc && gc->clip_mask &&
        ((TkpClipMask *)gc->clip_mask)->type == TKP_CLIP_REGION) {
        Region gcClip = ((TkpClipMask *)gc->clip_mask)->value.region;
        
        /* Calculate intersection of current clip with GC clip */
        /* This is simplified - actual region intersection needed */
        BoxPtr extents = RegionExtents(gcClip);
        
        if (extents) {
            int gc_x1 = extents->x1 + gc->clip_x_origin;
            int gc_y1 = extents->y1 + gc->clip_y_origin;
            int gc_x2 = extents->x2 + gc->clip_x_origin;
            int gc_y2 = extents->y2 + gc->clip_y_origin;
            
            /* Intersect rectangles */
            int x1 = (*clip_x > gc_x1) ? *clip_x : gc_x1;
            int y1 = (*clip_y > gc_y1) ? *clip_y : gc_y1;
            int x2 = ((*clip_x + *clip_width) < gc_x2) ?
                     (*clip_x + *clip_width) : gc_x2;
            int y2 = ((*clip_y + *clip_height) < gc_y2) ?
                     (*clip_y + *clip_height) : gc_y2;
            
            if (x1 < x2 && y1 < y2) {
                *clip_x = x1;
                *clip_y = y1;
                *clip_width = x2 - x1;
                *clip_height = y2 - y1;
            } else {
                *clip_width = 0;
                *clip_height = 0;
            }
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_DrawHighlightBorder --
 *
 *	This procedure draws a rectangular ring around the outside of a widget
 *	to indicate that it has received the input focus.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A rectangle "width" pixels wide is drawn.
 *
 *----------------------------------------------------------------------
 */

void
Tk_DrawHighlightBorder(
    Tk_Window tkwin,
    GC fgGC,
    GC bgGC,
    int highlightWidth,
    Drawable drawable)
{
    if (highlightWidth <= 1) {
        TkDrawInsetFocusHighlight(tkwin, fgGC, 1, drawable, 0);
    } else {
        TkDrawInsetFocusHighlight(tkwin, bgGC, highlightWidth, drawable, 0);
        if (fgGC != bgGC) {
            TkDrawInsetFocusHighlight(tkwin, fgGC, highlightWidth - 1,
                                      drawable, 0);
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkpDrawFrameEx --
 *
 *	This procedure draws the rectangular frame area.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Draws inside the tkwin area.
 *
 *----------------------------------------------------------------------
 */

void
TkpDrawFrameEx(
    Tk_Window tkwin,
    Drawable drawable,
    Tk_3DBorder border,
    int highlightWidth,
    int borderWidth,
    int relief)
{
    Tk_Fill3DRectangle(tkwin, drawable, border, highlightWidth,
                       highlightWidth, Tk_Width(tkwin) - 2 * highlightWidth,
                       Tk_Height(tkwin) - 2 * highlightWidth,
                       borderWidth, relief);
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * coding: utf-8
 * End:
 */
