/*
 * tkGlfwScrollbar.c --
 *
 *	This file implements the GLFW/Wayland specific portion of the scrollbar
 *	widget. Ported from tkUnixScrollbar.c
 *
 * Copyright © 1996 Sun Microsystems, Inc.
 * Copyright © 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkScrollbar.h"
#include <GLFW/glfw3.h>

/*
 * Minimum slider length, in pixels (designed to make sure that the slider is
 * always easy to grab with the mouse).
 */

#define MIN_SLIDER_LENGTH	5

/*
 * Declaration of GLFW specific scrollbar structure.
 */

typedef struct GlfwScrollbar {
    TkScrollbar info;		/* Generic scrollbar info. */
    GLFWwindow *glfwWindow;	/* GLFW window handle */
    int needsRedraw;		/* Flag for pending redraws */
    void *renderContext;	/* NanoVG or other rendering context */
} GlfwScrollbar;

/*
 * Forward declarations for GLFW event callbacks
 */
static void GlfwExposeCallback(GLFWwindow *window);
static void GlfwResizeCallback(GLFWwindow *window, int width, int height);
static void GlfwFocusCallback(GLFWwindow *window, int focused);
static void GlfwMouseButtonCallback(GLFWwindow *window, int button, int action, int mods);
static void GlfwCursorPosCallback(GLFWwindow *window, double xpos, double ypos);

/*
 * The class procedure table for the scrollbar widget. All fields except size
 * are left initialized to NULL, which should happen automatically since the
 * variable is declared at this scope.
 */

const Tk_ClassProcs tkpScrollbarProcs = {
    sizeof(Tk_ClassProcs),	/* size */
    NULL,					/* worldChangedProc */
    NULL,					/* createProc */
    NULL					/* modalProc */
};

/*
 *----------------------------------------------------------------------
 *
 * TkpCreateScrollbar --
 *
 *	Allocate a new TkScrollbar structure.
 *
 * Results:
 *	Returns a newly allocated TkScrollbar structure.
 *
 * Side effects:
 *	Registers event handlers for the widget using GLFW callbacks.
 *
 *----------------------------------------------------------------------
 */

TkScrollbar *
TkpCreateScrollbar(
    Tk_Window tkwin)
{
    GlfwScrollbar *scrollPtr = (GlfwScrollbar *)Tcl_Alloc(sizeof(GlfwScrollbar));

    scrollPtr->glfwWindow = NULL;
    scrollPtr->needsRedraw = 1;
    scrollPtr->renderContext = NULL;

    /*
     * Get or create GLFW window for this Tk_Window
     * This assumes you have a function to get the GLFWwindow* from Tk_Window
     */
    scrollPtr->glfwWindow = (GLFWwindow *)Tk_GetNativeWindow(tkwin);
    
    if (scrollPtr->glfwWindow != NULL) {
        /*
         * Set up GLFW event callbacks
         * Store scrollPtr as user pointer for callback access
         */
        glfwSetWindowUserPointer(scrollPtr->glfwWindow, scrollPtr);
        
        /*
         * Register callbacks equivalent to:
         * ExposureMask|StructureNotifyMask|FocusChangeMask
         */
        glfwSetWindowRefreshCallback(scrollPtr->glfwWindow, GlfwExposeCallback);
        glfwSetWindowSizeCallback(scrollPtr->glfwWindow, GlfwResizeCallback);
        glfwSetWindowFocusCallback(scrollPtr->glfwWindow, GlfwFocusCallback);
        glfwSetMouseButtonCallback(scrollPtr->glfwWindow, GlfwMouseButtonCallback);
        glfwSetCursorPosCallback(scrollPtr->glfwWindow, GlfwCursorPosCallback);
    }

    return (TkScrollbar *) scrollPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * GLFW Event Callbacks --
 *
 *	These functions handle GLFW events and translate them to Tk events.
 *
 *----------------------------------------------------------------------
 */

static void
GlfwExposeCallback(GLFWwindow *window)
{
    GlfwScrollbar *scrollPtr = (GlfwScrollbar *)glfwGetWindowUserPointer(window);
    if (scrollPtr != NULL) {
        scrollPtr->needsRedraw = 1;
        /* Trigger TkScrollbarEventProc equivalent for expose events */
        TkpDisplayScrollbar((TkScrollbar *)scrollPtr);
    }
}

static void
GlfwResizeCallback(GLFWwindow *window, int width, int height)
{
    GlfwScrollbar *scrollPtr = (GlfwScrollbar *)glfwGetWindowUserPointer(window);
    if (scrollPtr != NULL) {
        /* Trigger geometry recomputation */
        TkpComputeScrollbarGeometry((TkScrollbar *)scrollPtr);
        scrollPtr->needsRedraw = 1;
    }
}

static void
GlfwFocusCallback(GLFWwindow *window, int focused)
{
    GlfwScrollbar *scrollPtr = (GlfwScrollbar *)glfwGetWindowUserPointer(window);
    TkScrollbar *tkScrollPtr = (TkScrollbar *)scrollPtr;
    
    if (scrollPtr != NULL && tkScrollPtr != NULL) {
        if (focused) {
            tkScrollPtr->flags |= GOT_FOCUS;
        } else {
            tkScrollPtr->flags &= ~GOT_FOCUS;
        }
        scrollPtr->needsRedraw = 1;
    }
}

static void
GlfwMouseButtonCallback(GLFWwindow *window, int button, int action, int mods)
{
    GlfwScrollbar *scrollPtr = (GlfwScrollbar *)glfwGetWindowUserPointer(window);
    if (scrollPtr != NULL) {
        /* Handle mouse button events for scrollbar interaction */
        /* This would typically call into TkScrollbarEventProc or similar */
        scrollPtr->needsRedraw = 1;
    }
}

static void
GlfwCursorPosCallback(GLFWwindow *window, double xpos, double ypos)
{
    GlfwScrollbar *scrollPtr = (GlfwScrollbar *)glfwGetWindowUserPointer(window);
    if (scrollPtr != NULL) {
        /* Handle mouse motion for hover effects */
        scrollPtr->needsRedraw = 1;
    }
}

/*
 *--------------------------------------------------------------
 *
 * TkpDisplayScrollbar --
 *
 *	This procedure redraws the contents of a scrollbar window using
 *	NanoVG rendering calls. It is invoked as a do-when-idle handler,
 *	so it only runs when there's nothing else for the application to do.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Information appears on the screen via NanoVG/GLFW rendering.
 *
 *--------------------------------------------------------------
 */

void
TkpDisplayScrollbar(
    void *clientData)	/* Information about window. */
{
    TkScrollbar *scrollPtr = (TkScrollbar *)clientData;
    GlfwScrollbar *glfwScrollPtr = (GlfwScrollbar *)clientData;
    Tk_Window tkwin = scrollPtr->tkwin;
    int width, elementBorderWidth;
    int borderWidth, highlightWidth;

    if ((scrollPtr->tkwin == NULL) || !Tk_IsMapped(tkwin)) {
	goto done;
    }

    if (glfwScrollPtr->glfwWindow == NULL) {
        goto done;
    }

    /* Make context current for rendering */
    glfwMakeContextCurrent(glfwScrollPtr->glfwWindow);

    if (scrollPtr->vertical) {
	width = Tk_Width(tkwin) - 2 * scrollPtr->inset;
    } else {
	width = Tk_Height(tkwin) - 2 * scrollPtr->inset;
    }
    
    Tk_GetPixelsFromObj(NULL, scrollPtr->tkwin, scrollPtr->borderWidthObj, &borderWidth);
    if (scrollPtr->elementBorderWidthObj) {
	Tk_GetPixelsFromObj(NULL, scrollPtr->tkwin, scrollPtr->elementBorderWidthObj, &elementBorderWidth);
    } else {
	elementBorderWidth = borderWidth;
    }

    /*
     * All rendering is now done via NanoVG calls (assumed to be implemented)
     * The actual NanoVG rendering calls would replace the X11 drawing code.
     * This section assumes those are implemented elsewhere as per your requirements.
     */

    /* Get window dimensions */
    int winWidth, winHeight;
    glfwGetWindowSize(glfwScrollPtr->glfwWindow, &winWidth, &winHeight);

    /*
     * NOTE: The actual rendering calls (nvgBeginFrame, nvgFillRect, etc.)
     * are assumed to be implemented in your NanoVG integration layer.
     * The structure and logic remain the same as the original X11 version,
     * but using NanoVG primitives instead of X11 primitives.
     */

    /* Begin frame */
    // nvgBeginFrame(vg, winWidth, winHeight, 1.0f);

    Tk_GetPixelsFromObj(NULL, scrollPtr->tkwin, scrollPtr->highlightWidthObj, &highlightWidth);
    
    /* Draw focus highlight */
    if (highlightWidth > 0) {
        /* Use NanoVG to draw focus highlight */
    }

    /* Draw background border */
    // nvgBeginPath(vg);
    // nvgRect(vg, highlightWidth, highlightWidth, 
    //         winWidth - 2*highlightWidth, winHeight - 2*highlightWidth);
    // Draw 3D border effect with NanoVG

    /* Draw trough */
    // nvgBeginPath(vg);
    // nvgRect(vg, scrollPtr->inset, scrollPtr->inset,
    //         winWidth - 2*scrollPtr->inset, winHeight - 2*scrollPtr->inset);
    // nvgFill(vg);

    /* Draw top/left arrow */
    if (scrollPtr->vertical) {
        /* Draw upward pointing triangle */
    } else {
        /* Draw leftward pointing triangle */
    }

    /* Draw bottom/right arrow */
    if (scrollPtr->vertical) {
        /* Draw downward pointing triangle */
    } else {
        /* Draw rightward pointing triangle */
    }

    /* Draw slider */
    if (scrollPtr->vertical) {
        /* Draw vertical slider rectangle */
        // nvgBeginPath(vg);
        // nvgRect(vg, scrollPtr->inset, scrollPtr->sliderFirst,
        //         width, scrollPtr->sliderLast - scrollPtr->sliderFirst);
        // nvgFill(vg);
    } else {
        /* Draw horizontal slider rectangle */
        // nvgBeginPath(vg);
        // nvgRect(vg, scrollPtr->sliderFirst, scrollPtr->inset,
        //         scrollPtr->sliderLast - scrollPtr->sliderFirst, width);
        // nvgFill(vg);
    }

    /* End frame and swap buffers */
    // nvgEndFrame(vg);
    glfwSwapBuffers(glfwScrollPtr->glfwWindow);

  done:
    scrollPtr->flags &= ~REDRAW_PENDING;
    glfwScrollPtr->needsRedraw = 0;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpComputeScrollbarGeometry --
 *
 *	After changes in a scrollbar's size or configuration, this procedure
 *	recomputes various geometry information used in displaying the
 *	scrollbar.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The scrollbar will be displayed differently.
 *
 *----------------------------------------------------------------------
 */

extern void
TkpComputeScrollbarGeometry(
    TkScrollbar *scrollPtr)
				/* Scrollbar whose geometry may have
				 * changed. */
{
    int width, fieldLength;
    int borderWidth, highlightWidth;

    Tk_GetPixelsFromObj(NULL, scrollPtr->tkwin, scrollPtr->borderWidthObj, &borderWidth);
    Tk_GetPixelsFromObj(NULL, scrollPtr->tkwin, scrollPtr->highlightWidthObj, &highlightWidth);
    scrollPtr->inset = highlightWidth + borderWidth;
    width = (scrollPtr->vertical) ? Tk_Width(scrollPtr->tkwin)
	    : Tk_Height(scrollPtr->tkwin);

    /*
     * Next line assumes that the arrow area is a square.
     */

    scrollPtr->arrowLength = width - 2 * scrollPtr->inset + 1;
    fieldLength = (scrollPtr->vertical ? Tk_Height(scrollPtr->tkwin)
	    : Tk_Width(scrollPtr->tkwin))
	    - 2 * (scrollPtr->arrowLength + scrollPtr->inset);
    if (fieldLength < 0) {
	fieldLength = 0;
    }
    scrollPtr->sliderFirst = fieldLength*scrollPtr->firstFraction;
    scrollPtr->sliderLast = fieldLength*scrollPtr->lastFraction;

    /*
     * Adjust the slider so that some piece of it is always displayed in the
     * scrollbar and so that it has at least a minimal width (so it can be
     * grabbed with the mouse).
     */

    if (scrollPtr->sliderFirst > fieldLength - MIN_SLIDER_LENGTH) {
	scrollPtr->sliderFirst = fieldLength - MIN_SLIDER_LENGTH;
    }
    if (scrollPtr->sliderFirst < 0) {
	scrollPtr->sliderFirst = 0;
    }
    if (scrollPtr->sliderLast < scrollPtr->sliderFirst + MIN_SLIDER_LENGTH) {
	scrollPtr->sliderLast = scrollPtr->sliderFirst + MIN_SLIDER_LENGTH;
    }
    if (scrollPtr->sliderLast > fieldLength) {
	scrollPtr->sliderLast = fieldLength;
    }
    scrollPtr->sliderFirst += scrollPtr->arrowLength + scrollPtr->inset;
    scrollPtr->sliderLast += scrollPtr->arrowLength + scrollPtr->inset;

    /*
     * Register the desired geometry for the window (leave enough space for
     * the two arrows plus a minimum-size slider, plus border around the whole
     * window, if any). Then arrange for the window to be redisplayed.
     */

    Tk_GetPixelsFromObj(NULL, scrollPtr->tkwin, scrollPtr->widthObj, &width);
    if (scrollPtr->vertical) {
	Tk_GeometryRequest(scrollPtr->tkwin,
		width + 2 * scrollPtr->inset,
		2 * (scrollPtr->arrowLength + borderWidth
		+ scrollPtr->inset));
    } else {
	Tk_GeometryRequest(scrollPtr->tkwin,
		2 * (scrollPtr->arrowLength + borderWidth
		+ scrollPtr->inset), width + 2 * scrollPtr->inset);
    }
    Tk_SetInternalBorder(scrollPtr->tkwin, scrollPtr->inset);
}

/*
 *----------------------------------------------------------------------
 *
 * TkpDestroyScrollbar --
 *
 *	Free data structures associated with the scrollbar control.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees GLFW resources and callbacks associated with the scrollbar.
 *
 *----------------------------------------------------------------------
 */

void
TkpDestroyScrollbar(
    TkScrollbar *scrollPtr)
{
    GlfwScrollbar *glfwScrollPtr = (GlfwScrollbar *)scrollPtr;

    if (glfwScrollPtr->glfwWindow != NULL) {
        /*
         * Clear callbacks to prevent dangling pointer access
         */
        glfwSetWindowUserPointer(glfwScrollPtr->glfwWindow, NULL);
        glfwSetWindowRefreshCallback(glfwScrollPtr->glfwWindow, NULL);
        glfwSetWindowSizeCallback(glfwScrollPtr->glfwWindow, NULL);
        glfwSetWindowFocusCallback(glfwScrollPtr->glfwWindow, NULL);
        glfwSetMouseButtonCallback(glfwScrollPtr->glfwWindow, NULL);
        glfwSetCursorPosCallback(glfwScrollPtr->glfwWindow, NULL);
        
        /*
         * Note: We don't destroy the GLFW window here as it may be shared
         * with other Tk widgets. Window lifetime is managed by Tk.
         */
    }

    if (glfwScrollPtr->renderContext != NULL) {
        /* Free any NanoVG or rendering context resources */
        glfwScrollPtr->renderContext = NULL;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkpConfigureScrollbar --
 *
 *	This procedure is called after the generic code has finished
 *	processing configuration options, in order to configure platform
 *	specific options.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Configuration info may get changed. Rendering context updated.
 *
 *----------------------------------------------------------------------
 */

void
TkpConfigureScrollbar(
    TkScrollbar *scrollPtr)
				/* Information about widget; may or may not
				 * already have values for some fields. */
{
    GlfwScrollbar *glfwScrollPtr = (GlfwScrollbar *) scrollPtr;

    Tk_SetBackgroundFromBorder(scrollPtr->tkwin, scrollPtr->bgBorder);

    /*
     * Update rendering context with new colors/settings
     * This replaces the X11 GC creation with NanoVG style/color updates
     */
    if (glfwScrollPtr->renderContext != NULL) {
        /* Update NanoVG colors and styles based on scrollPtr configuration */
        /* Example: nvgFillColor(vg, nvgRGBA(r, g, b, a)); */
    }

    /* Mark for redraw with new configuration */
    glfwScrollPtr->needsRedraw = 1;
}

/*
 *--------------------------------------------------------------
 *
 * TkpScrollbarPosition --
 *
 *	Determine the scrollbar element corresponding to a given position.
 *
 * Results:
 *	One of TOP_ARROW, TOP_GAP, etc., indicating which element of the
 *	scrollbar covers the position given by (x, y). If (x,y) is outside the
 *	scrollbar entirely, then OUTSIDE is returned.
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */

int
TkpScrollbarPosition(
    TkScrollbar *scrollPtr,
				/* Scrollbar widget record. */
    int x, int y)		/* Coordinates within scrollPtr's window. */
{
    int length, width, tmp;
    const int inset = scrollPtr->inset;

    if (scrollPtr->vertical) {
	length = Tk_Height(scrollPtr->tkwin);
	width = Tk_Width(scrollPtr->tkwin);
    } else {
	tmp = x;
	x = y;
	y = tmp;
	length = Tk_Width(scrollPtr->tkwin);
	width = Tk_Height(scrollPtr->tkwin);
    }

    if (x<inset || x>=width-inset || y<inset || y>=length-inset) {
	return OUTSIDE;
    }

    /*
     * All of the calculations in this procedure mirror those in
     * TkpDisplayScrollbar. Be sure to keep the two consistent.
     */

    if (y < inset + scrollPtr->arrowLength) {
	return TOP_ARROW;
    }
    if (y < scrollPtr->sliderFirst) {
	return TOP_GAP;
    }
    if (y < scrollPtr->sliderLast) {
	return SLIDER;
    }
    if (y >= length - (scrollPtr->arrowLength + inset)) {
	return BOTTOM_ARROW;
    }
    return BOTTOM_GAP;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */