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
#include "nanovg_gl.h"  /* Add GL implementation header for nvgCreateGL3/nvgDeleteGL3 */

/* GLFW Integration. */
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WAYLAND
#include <GLFW/glfw3native.h>
#include "tkGlfwInt.h"

/* OpenGL for NanoVG. */
#include <GL/gl.h>

/* X11 region headers for BoxPtr and Region types */
#include <X11/Xutil.h>

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
static NVGcontext* GetNVGContextForDrawable(Drawable drawable);
static WaylandDrawable* GetWaylandDrawable(Drawable drawable);
static void RegisterDrawable(Drawable tkDrawable, WaylandDrawable* waylandDrawable);
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
    (void)display;  /* Mark unused parameter to suppress warning */
    
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
 * RegisterDrawable --
 *
 *	Register a Wayland drawable in the global map.
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
    DrawableMap* newEntry = (DrawableMap*)ckalloc(sizeof(DrawableMap));
    newEntry->tkDrawable = tkDrawable;
    newEntry->waylandDrawable = waylandDrawable;
    newEntry->next = drawableMap;
    drawableMap = newEntry;
}

/*
 *----------------------------------------------------------------------
 *
 * GetWaylandDrawable --
 *
 *	Retrieve Wayland drawable from Tk drawable.
 *
 * Results:
 *	WaylandDrawable pointer or NULL if not found.
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
 *	Get the NanoVG context for a drawable.
 *
 * Results:
 *	NanoVG context or NULL.
 *
 * Side effects:
 *	May make the drawable's GLFW window current.
 *
 *----------------------------------------------------------------------
 */

static NVGcontext*
GetNVGContextForDrawable(
    Drawable drawable)
{
    WaylandDrawable* wd = GetWaylandDrawable(drawable);
    
    if (wd && wd->glfwWindow) {
        glfwMakeContextCurrent(wd->glfwWindow);
        return globalVGContext;
    }
    
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandCreateDrawable --
 *
 *	Create a new Wayland drawable with GLFW window.
 *
 * Results:
 *	WaylandDrawable pointer or NULL on failure.
 *
 * Side effects:
 *	Creates GLFW window and registers drawable.
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
    
    if (!glfwInitialized) {
        return NULL;
    }
    
    wd = (WaylandDrawable*)ckalloc(sizeof(WaylandDrawable));
    wd->width = width;
    wd->height = height;
    
    /* Create GLFW window sharing context with global window. */
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    wd->glfwWindow = glfwCreateWindow(width, height, title, NULL, globalGLFWWindow);
    
    if (!wd->glfwWindow) {
        ckfree((char*)wd);
        return NULL;
    }
    
    glfwSetWindowUserPointer(wd->glfwWindow, wd);
    glfwSetFramebufferSizeCallback(wd->glfwWindow, GLFWFramebufferSizeCallback);
    
    RegisterDrawable(tkDrawable, wd);
    
    return wd;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_WaylandGetGLFWWindow --
 *
 *	Get GLFW window for a drawable.
 *
 * Results:
 *	GLFWwindow pointer or NULL.
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
 * Tk_WaylandGetContextForDrawable --
 *
 *	Get NanoVG context for a drawable.
 *
 * Results:
 *	NVGcontext pointer or NULL.
 *
 * Side effects:
 *	May make the drawable's GLFW window current.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void*
Tk_WaylandGetContextForDrawable(
    Drawable drawable)
{
    return GetNVGContextForDrawable(drawable);
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandPollEvents --
 *
 *	Poll for Wayland/GLFW events.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Processes pending GLFW events.
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
 * TkWaylandSetupDrawingContext --
 *
 *	Set up drawing context for a drawable with a GC.
 *
 * Results:
 *	true if successful, false otherwise.
 *
 * Side effects:
 *	Initializes drawing context, sets up NanoVG state.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE bool
TkWaylandSetupDrawingContext(
    Drawable d,
    GC gc,
    TkWaylandDrawingContext *dcPtr)
{
    NVGcontext* vg;
    XColor xcolor;
    XGCValues gcValues;
    Tcl_Obj *colorObj;
    
    if (!dcPtr) {
        return false;
    }
    
    dcPtr->vg = GetNVGContextForDrawable(d);
    dcPtr->drawable = d;  /* Now this will work with the added field */
    vg = dcPtr->vg;
    
    if (!vg) {
        return false;
    }
    
    /* Get GC values using XGetGCValues instead of direct struct access */
    if (!XGetGCValues(NULL, gc, 
                      GCForeground | GCLineWidth | GCLineStyle | 
                      GCCapStyle | GCJoinStyle | GCFillRule | GCArcMode,
                      &gcValues)) {
        return false;
    }
    
    /* Begin NanoVG frame. */
    WaylandDrawable* wd = GetWaylandDrawable(d);
    if (wd) {
        nvgBeginFrame(vg, wd->width, wd->height, 1.0f);
    }
    
    /* Set up foreground color using Tk_GetColorFromObj correctly */
    if (gcValues.foreground) {
        /* Create a Tcl_Obj for the pixel value */
        char colorString[32];
        snprintf(colorString, sizeof(colorString), "#%06lx", gcValues.foreground);
        colorObj = Tcl_NewStringObj(colorString, -1);
        Tcl_IncrRefCount(colorObj);
        
        XColor *colorPtr = Tk_GetColorFromObj(NULL, colorObj);
        if (colorPtr) {
            xcolor = *colorPtr;
            nvgFillColor(vg, nvgRGBA(xcolor.red >> 8, xcolor.green >> 8,
                                      xcolor.blue >> 8, 255));
            nvgStrokeColor(vg, nvgRGBA(xcolor.red >> 8, xcolor.green >> 8,
                                        xcolor.blue >> 8, 255));
        }
        Tcl_DecrRefCount(colorObj);
    }
    
    /* Set stroke width */
    if (gcValues.line_width > 0) {
        nvgStrokeWidth(vg, gcValues.line_width);
    } else {
        nvgStrokeWidth(vg, 1.0f);
    }
    
    /* Set line style */
    switch (gcValues.line_style) {
        case LineSolid:
            /* Solid line - no special handling needed */
            break;
        case LineOnOffDash:
        case LineDoubleDash:
            /* TODO: Implement dashed lines if needed */
            break;
    }
    
    /* Set cap style */
    switch (gcValues.cap_style) {
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
    
    /* Set join style */
    switch (gcValues.join_style) {
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
    
    return true;
}

/*
 *----------------------------------------------------------------------
 *
 * TkWaylandRestoreDrawingContext --
 *
 *	Clean up drawing context.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Ends NanoVG frame and swaps buffers.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE void
TkWaylandRestoreDrawingContext(
    TkWaylandDrawingContext *dcPtr)
{
    if (dcPtr && dcPtr->vg) {
        nvgEndFrame(dcPtr->vg);
        
        WaylandDrawable* wd = GetWaylandDrawable(dcPtr->drawable);  /* Now this will work */
        if (wd && wd->glfwWindow) {
            glfwSwapBuffers(wd->glfwWindow);
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * XDrawLines --
 *
 *	Draw connected line segments.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	Draws lines on the specified drawable.
 *
 *----------------------------------------------------------------------
 */

int
XDrawLines(
    Display *display,
    Drawable d,
    GC gc,
    XPoint *points,
    int npoints,
    int mode)
{
    TkWaylandDrawingContext dc;
    int i;
    
    if (npoints < 2) {
        return BadValue;
    }
    
    LastKnownRequestProcessed(display)++;
    if (!TkWaylandSetupDrawingContext(d, gc, &dc)) {
        return BadDrawable;
    }
    
    if (dc.vg) {
        nvgBeginPath(dc.vg);
        nvgMoveTo(dc.vg, points[0].x, points[0].y);
        
        for (i = 1; i < npoints; i++) {
            if (mode == CoordModeOrigin) {
                nvgLineTo(dc.vg, points[i].x, points[i].y);
            } else {  /* CoordModePrevious */
                nvgLineTo(dc.vg, points[i-1].x + points[i].x,
                          points[i-1].y + points[i].y);
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
 *	Draw multiple unconnected line segments.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	Draws line segments on the specified drawable.
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
 *	Fill a polygon.
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
    Display *display,
    Drawable d,
    GC gc,
    XPoint *points,
    int npoints,
    int shape,
    int mode)
{
    TkWaylandDrawingContext dc;
    XGCValues gcValues;
    int i;
    
    if (npoints < 3) {
        return BadValue;
    }
    
    LastKnownRequestProcessed(display)++;
    if (!TkWaylandSetupDrawingContext(d, gc, &dc)) {
        return BadDrawable;
    }
    
    /* Get GC values for fill rule */
    XGetGCValues(NULL, gc, GCFillRule, &gcValues);
    
    if (dc.vg) {
        nvgBeginPath(dc.vg);
        nvgMoveTo(dc.vg, points[0].x, points[0].y);
        
        for (i = 1; i < npoints; i++) {
            if (mode == CoordModeOrigin) {
                nvgLineTo(dc.vg, points[i].x, points[i].y);
            } else {  /* CoordModePrevious */
                nvgLineTo(dc.vg, points[i-1].x + points[i].x,
                          points[i-1].y + points[i].y);
            }
        }
        
        nvgClosePath(dc.vg);
        
        /* Set winding based on fill rule */
        if (gcValues.fill_rule == EvenOddRule) {
            nvgPathWinding(dc.vg, NVG_HOLE);
        } else {
            nvgPathWinding(dc.vg, NVG_SOLID);
        }
        
        nvgFill(dc.vg);
    }
    
    (void)shape;  /* Suppress unused warning */
    TkWaylandRestoreDrawingContext(&dc);
    return Success;
}

/*
 *----------------------------------------------------------------------
 *
 * XDrawRectangle --
 *
 *	Draw a rectangle outline.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	Draws a rectangle outline on the specified drawable.
 *
 *----------------------------------------------------------------------
 */

int
XDrawRectangle(
    Display *display,
    Drawable d,
    GC gc,
    int x, int y,
    unsigned int width,
    unsigned int height)
{
    TkWaylandDrawingContext dc;
    
    if (width == 0 || height == 0) {
        return BadValue;
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
 *	Draw multiple rectangle outlines.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	Draws rectangle outlines on the specified drawable.
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
    int i;
    
    LastKnownRequestProcessed(display)++;
    if (!TkWaylandSetupDrawingContext(d, gc, &dc)) {
        return BadDrawable;
    }
    
    if (dc.vg) {
        for (i = 0; i < nRects; i++) {
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
 *	Fill multiple rectangles.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	Draws filled rectangles on the specified drawable.
 *
 *----------------------------------------------------------------------
 */

int
XFillRectangles(
    Display *display,
    Drawable d,
    GC gc,
    XRectangle *rectangles,
    int n_rectangles)
{
    TkWaylandDrawingContext dc;
    int i;
    
    LastKnownRequestProcessed(display)++;
    if (!TkWaylandSetupDrawingContext(d, gc, &dc)) {
        return BadDrawable;
    }
    
    if (dc.vg) {
        for (i = 0; i < n_rectangles; i++) {
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
    Display *display,
    Drawable d,
    GC gc,
    int x, int y,
    unsigned int width,
    unsigned int height,
    int angle1,
    int angle2)
{
    TkWaylandDrawingContext dc;
    
    if (width == 0 || height == 0 || angle2 == 0) {
        return BadValue;
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
        
        if (width == height) {
            nvgArc(dc.vg, cx, cy, rx, startAngle, endAngle, NVG_CW);
        } else {
            /* Ellipse: scale transform */
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
 *	Draw multiple arcs.
 *
 * Results:
 *	Success.
 *
 * Side effects:
 *	Draws arcs on the specified drawable.
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
    XGCValues gcValues;
    
    if (width == 0 || height == 0 || angle2 == 0 || !wd) {
        return BadDrawable;
    }
    
    LastKnownRequestProcessed(display)++;
    if (!TkWaylandSetupDrawingContext(d, gc, &dc)) {
        return BadDrawable;
    }
    
    /* Get GC values for arc mode */
    XGetGCValues(NULL, gc, GCArcMode, &gcValues);
    
    if (dc.vg) {
        float cx = x + width / 2.0f;
        float cy = y + height / 2.0f;
        float rx = width / 2.0f;
        float ry = height / 2.0f;
        
        float startAngle = -radians(angle1 / 64.0);
        float endAngle = -radians((angle1 + angle2) / 64.0);
        
        nvgBeginPath(dc.vg);
        
        if (gcValues.arc_mode == ArcPieSlice) {
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
        
        if (gcValues.arc_mode == ArcPieSlice) {
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
    XGCValues gcValues;
    int i;
    
    LastKnownRequestProcessed(display)++;
    if (!TkWaylandSetupDrawingContext(d, gc, &dc)) {
        return BadDrawable;
    }
    
    /* Get GC values for arc mode */
    XGetGCValues(NULL, gc, GCArcMode, &gcValues);
    
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
            
            if (gcValues.arc_mode == ArcPieSlice) {
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
            
            if (gcValues.arc_mode == ArcPieSlice) {
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
