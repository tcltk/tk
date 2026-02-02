/*
 * tkWaylandScrollbar.c --
 *
 *	This file implements the wayland specific portion of the scrollbar
 *	widget.
 *
 * Copyright © 1996 Sun Microsystems, Inc.
 * Copyright © 2026 Kevin Walzer
 
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
 * Declaration of Wayland specific scrollbar structure.
 */

typedef struct WaylandScrollbar {
    TkScrollbar info;		/* Generic scrollbar info. */
    GC troughGC;		/* For drawing trough. */
    GC copyGC;			/* Used for copying from pixmap onto screen. */
    int dragStartX;             /* X position when drag started */
    int dragStartY;             /* Y position when drag started */
    int dragStartFirst;         /* sliderFirst when drag started */
} WaylandScrollbar;

/*
 * Structure to manage scrollbars within a GLFW window
 */
typedef struct {
    Tk_Window tkwin;
    WaylandScrollbar **scrollbars;
    int scrollbarCount;
} WaylandWindowData;

/*
 * Additional scrollbar flags
 */
#define SLIDER_DRAGGING  0x1000

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
 * Forward declarations for GLFW callbacks
 */
static void GLFW_FramebufferSizeCallback(GLFWwindow* window, int width, int height);
static void GLFW_WindowSizeCallback(GLFWwindow* window, int width, int height);
static void GLFW_WindowFocusCallback(GLFWwindow* window, int focused);
static void GLFW_CursorPosCallback(GLFWwindow* window, double xpos, double ypos);
static void GLFW_MouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
static void GLFW_ScrollCallback(GLFWwindow* window, double xoffset, double yoffset);
static void WaylandScrollbar_AddToWindow(GLFWwindow *glfwWindow, WaylandScrollbar *scrollbar);
static void WaylandScrollbar_RemoveFromWindow(GLFWwindow *glfwWindow, WaylandScrollbar *scrollbar);
static GLFWwindow *GetGLFWWindowFromTkWindow(Tk_Window tkwin);

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
 *	Registers an event handler for the widget.
 *
 *----------------------------------------------------------------------
 */

TkScrollbar *
TkpCreateScrollbar(
    Tk_Window tkwin)
{
    WaylandScrollbar *scrollPtr = (WaylandScrollbar *)Tcl_Alloc(sizeof(WaylandScrollbar));

    scrollPtr->troughGC = NULL;
    scrollPtr->copyGC = NULL;
    scrollPtr->dragStartX = 0;
    scrollPtr->dragStartY = 0;
    scrollPtr->dragStartFirst = 0;

    /* 
     * Find the GLFW window for this Tk window and add scrollbar to it.
     * The GLFW callbacks are set up once per window in the window creation code.
     */
    GLFWwindow *glfwWindow = GetGLFWWindowFromTkWindow(tkwin);
    if (glfwWindow) {
        WaylandScrollbar_AddToWindow(glfwWindow, scrollPtr);
    }

    return (TkScrollbar *) scrollPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * WaylandScrollbar_SetupGLFWCallbacks --
 *
 *	Set up GLFW callbacks for a window containing scrollbar widgets.
 *	This should be called once per GLFW window, not per scrollbar.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	GLFW callbacks are registered.
 *
 *----------------------------------------------------------------------
 */

void
WaylandScrollbar_SetupGLFWCallbacks(
    GLFWwindow *glfwWindow,
    Tk_Window tkwin)
{
    WaylandWindowData *windowData = (WaylandWindowData *)Tcl_Alloc(sizeof(WaylandWindowData));
    
    windowData->tkwin = tkwin;
    windowData->scrollbars = NULL;
    windowData->scrollbarCount = 0;
    
    glfwSetWindowUserPointer(glfwWindow, windowData);
    
    glfwSetFramebufferSizeCallback(glfwWindow, GLFW_FramebufferSizeCallback);
    glfwSetWindowSizeCallback(glfwWindow, GLFW_WindowSizeCallback);
    glfwSetWindowFocusCallback(glfwWindow, GLFW_WindowFocusCallback);
    glfwSetCursorPosCallback(glfwWindow, GLFW_CursorPosCallback);
    glfwSetMouseButtonCallback(glfwWindow, GLFW_MouseButtonCallback);
    glfwSetScrollCallback(glfwWindow, GLFW_ScrollCallback);
}

/*
 *----------------------------------------------------------------------
 *
 * GLFW_FramebufferSizeCallback --
 *
 *	Handle framebuffer size changes.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Scrollbars are resized and redrawn.
 *
 *----------------------------------------------------------------------
 */

static void
GLFW_FramebufferSizeCallback(
    GLFWwindow* window,
    int width,
    int height)
{
    WaylandWindowData *windowData = (WaylandWindowData *)glfwGetWindowUserPointer(window);
    
    if (!windowData) return;
    
    for (int i = 0; i < windowData->scrollbarCount; i++) {
        WaylandScrollbar *scrollPtr = windowData->scrollbars[i];
        if (scrollPtr && scrollPtr->info.tkwin == windowData->tkwin) {
            TkpComputeScrollbarGeometry((TkScrollbar *)scrollPtr);
            if (!(scrollPtr->info.flags & REDRAW_PENDING)) {
                scrollPtr->info.flags |= REDRAW_PENDING;
                Tk_DoWhenIdle(TkpDisplayScrollbar, (void *)scrollPtr);
            }
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * GLFW_WindowSizeCallback --
 *
 *	Handle window size changes.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Scrollbars are resized.
 *
 *----------------------------------------------------------------------
 */

static void
GLFW_WindowSizeCallback(
    GLFWwindow* window,
    int width,
    int height)
{
    WaylandWindowData *windowData = (WaylandWindowData *)glfwGetWindowUserPointer(window);
    
    if (!windowData) return;
    
    for (int i = 0; i < windowData->scrollbarCount; i++) {
        WaylandScrollbar *scrollPtr = windowData->scrollbars[i];
        if (scrollPtr && scrollPtr->info.tkwin == windowData->tkwin) {
            TkpComputeScrollbarGeometry((TkScrollbar *)scrollPtr);
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * GLFW_WindowFocusCallback --
 *
 *	Handle window focus changes.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Scrollbar focus state is updated.
 *
 *----------------------------------------------------------------------
 */

static void
GLFW_WindowFocusCallback(
    GLFWwindow* window,
    int focused)
{
    WaylandWindowData *windowData = (WaylandWindowData *)glfwGetWindowUserPointer(window);
    
    if (!windowData) return;
    
    for (int i = 0; i < windowData->scrollbarCount; i++) {
        WaylandScrollbar *scrollPtr = windowData->scrollbars[i];
        if (scrollPtr && scrollPtr->info.tkwin == windowData->tkwin) {
            if (focused) {
                scrollPtr->info.flags |= GOT_FOCUS;
            } else {
                scrollPtr->info.flags &= ~GOT_FOCUS;
            }
            
            if (!(scrollPtr->info.flags & REDRAW_PENDING)) {
                scrollPtr->info.flags |= REDRAW_PENDING;
                Tk_DoWhenIdle(TkpDisplayScrollbar, (void *)scrollPtr);
            }
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * GLFW_CursorPosCallback --
 *
 *	Handle mouse movement.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Scrollbar hover state may change.
 *
 *----------------------------------------------------------------------
 */

static void
GLFW_CursorPosCallback(
    GLFWwindow* window,
    double xpos,
    double ypos)
{
    WaylandWindowData *windowData = (WaylandWindowData *)glfwGetWindowUserPointer(window);
    
    if (!windowData) return;
    
    for (int i = 0; i < windowData->scrollbarCount; i++) {
        WaylandScrollbar *scrollPtr = windowData->scrollbars[i];
        if (scrollPtr && scrollPtr->info.tkwin == windowData->tkwin) {
            int element = TkpScrollbarPosition((TkScrollbar *)scrollPtr, 
                                              (int)xpos, (int)ypos);
            
            if (element != scrollPtr->info.activeField) {
                scrollPtr->info.activeField = element;
                
                if (!(scrollPtr->info.flags & REDRAW_PENDING)) {
                    scrollPtr->info.flags |= REDRAW_PENDING;
                    Tk_DoWhenIdle(TkpDisplayScrollbar, (void *)scrollPtr);
                }
            }
            
            /* Handle slider dragging */
            if (scrollPtr->info.flags & SLIDER_DRAGGING) {
                int delta;
                int fieldLength;
                
                if (scrollPtr->info.vertical) {
                    fieldLength = Tk_Height(scrollPtr->info.tkwin) - 
                                  2 * (scrollPtr->info.arrowLength + scrollPtr->info.inset);
                    delta = (int)ypos - scrollPtr->dragStartY;
                } else {
                    fieldLength = Tk_Width(scrollPtr->info.tkwin) - 
                                  2 * (scrollPtr->info.arrowLength + scrollPtr->info.inset);
                    delta = (int)xpos - scrollPtr->dragStartX;
                }
                
                if (fieldLength > 0) {
                    double fractionDelta = (double)delta / fieldLength;
                    double newFirst = scrollPtr->info.firstFraction + fractionDelta;
                    double newLast = scrollPtr->info.lastFraction + fractionDelta;
                    
                    if (newFirst < 0.0) {
                        newFirst = 0.0;
                        newLast = scrollPtr->info.lastFraction - scrollPtr->info.firstFraction;
                    }
                    if (newLast > 1.0) {
                        newLast = 1.0;
                        newFirst = 1.0 - (scrollPtr->info.lastFraction - scrollPtr->info.firstFraction);
                    }
                    
                    scrollPtr->info.firstFraction = newFirst;
                    scrollPtr->info.lastFraction = newLast;
                    
                    TkpComputeScrollbarGeometry((TkScrollbar *)scrollPtr);
                    
                    /* Invoke Tcl command if configured */
                    if (scrollPtr->info.commandObj) {
                        Tcl_Obj *resultObj;
                        char string[200];
                        
                        sprintf(string, "%g %g", newFirst, newLast);
                        resultObj = Tcl_NewStringObj(string, -1);
                        Tcl_IncrRefCount(resultObj);
                        
                        if (Tcl_EvalObjEx(scrollPtr->info.interp, resultObj, 0) != TCL_OK) {
                            /* Handle error */
                        }
                        
                        Tcl_DecrRefCount(resultObj);
                    }
                    
                    if (!(scrollPtr->info.flags & REDRAW_PENDING)) {
                        scrollPtr->info.flags |= REDRAW_PENDING;
                        Tk_DoWhenIdle(TkpDisplayScrollbar, (void *)scrollPtr);
                    }
                }
            }
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * GLFW_MouseButtonCallback --
 *
 *	Handle mouse button events.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Scrollbar may trigger scroll actions.
 *
 *----------------------------------------------------------------------
 */

static void
GLFW_MouseButtonCallback(
    GLFWwindow* window,
    int button,
    int action,
    int mods)
{
    WaylandWindowData *windowData = (WaylandWindowData *)glfwGetWindowUserPointer(window);
    
    if (!windowData || button != GLFW_MOUSE_BUTTON_LEFT) return;
    
    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);
    
    for (int i = 0; i < windowData->scrollbarCount; i++) {
        WaylandScrollbar *scrollPtr = windowData->scrollbars[i];
        if (scrollPtr && scrollPtr->info.tkwin == windowData->tkwin) {
            int element = TkpScrollbarPosition((TkScrollbar *)scrollPtr, 
                                              (int)xpos, (int)ypos);
            
            if (action == GLFW_PRESS) {
                scrollPtr->info.activeField = element;
                scrollPtr->info.flags |= BUTTON_PRESSED;
                
                /* Store drag start position */
                scrollPtr->dragStartX = (int)xpos;
                scrollPtr->dragStartY = (int)ypos;
                scrollPtr->dragStartFirst = scrollPtr->info.sliderFirst;
                
                /* Trigger scroll action based on element */
                switch (element) {
                    case TOP_ARROW:
                        /* Scroll up/left - invoke Tcl command */
                        if (scrollPtr->info.commandObj) {
                            Tcl_Obj *resultObj = Tcl_NewStringObj("scroll", -1);
                            Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj("-1", -1));
                            Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj("units", -1));
                            Tcl_IncrRefCount(resultObj);
                            
                            if (Tcl_EvalObjEx(scrollPtr->info.interp, resultObj, 0) != TCL_OK) {
                                /* Handle error */
                            }
                            
                            Tcl_DecrRefCount(resultObj);
                        }
                        break;
                        
                    case BOTTOM_ARROW:
                        /* Scroll down/right - invoke Tcl command */
                        if (scrollPtr->info.commandObj) {
                            Tcl_Obj *resultObj = Tcl_NewStringObj("scroll", -1);
                            Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj("1", -1));
                            Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj("units", -1));
                            Tcl_IncrRefCount(resultObj);
                            
                            if (Tcl_EvalObjEx(scrollPtr->info.interp, resultObj, 0) != TCL_OK) {
                                /* Handle error */
                            }
                            
                            Tcl_DecrRefCount(resultObj);
                        }
                        break;
                        
                    case TOP_GAP:
                        /* Page up/left - invoke Tcl command */
                        if (scrollPtr->info.commandObj) {
                            Tcl_Obj *resultObj = Tcl_NewStringObj("scroll", -1);
                            Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj("-1", -1));
                            Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj("pages", -1));
                            Tcl_IncrRefCount(resultObj);
                            
                            if (Tcl_EvalObjEx(scrollPtr->info.interp, resultObj, 0) != TCL_OK) {
                                /* Handle error */
                            }
                            
                            Tcl_DecrRefCount(resultObj);
                        }
                        break;
                        
                    case BOTTOM_GAP:
                        /* Page down/right - invoke Tcl command */
                        if (scrollPtr->info.commandObj) {
                            Tcl_Obj *resultObj = Tcl_NewStringObj("scroll", -1);
                            Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj("1", -1));
                            Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj("pages", -1));
                            Tcl_IncrRefCount(resultObj);
                            
                            if (Tcl_EvalObjEx(scrollPtr->info.interp, resultObj, 0) != TCL_OK) {
                                /* Handle error */
                            }
                            
                            Tcl_DecrRefCount(resultObj);
                        }
                        break;
                        
                    case SLIDER:
                        /* Start dragging */
                        scrollPtr->info.flags |= SLIDER_DRAGGING;
                        break;
                }
            } else if (action == GLFW_RELEASE) {
                scrollPtr->info.flags &= ~(BUTTON_PRESSED | SLIDER_DRAGGING);
                scrollPtr->info.activeField = OUTSIDE;
            }
            
            if (!(scrollPtr->info.flags & REDRAW_PENDING)) {
                scrollPtr->info.flags |= REDRAW_PENDING;
                Tk_DoWhenIdle(TkpDisplayScrollbar, (void *)scrollPtr);
            }
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * GLFW_ScrollCallback --
 *
 *	Handle scroll wheel events.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Scrollbar may scroll.
 *
 *----------------------------------------------------------------------
 */

static void
GLFW_ScrollCallback(
    GLFWwindow* window,
    double xoffset,
    double yoffset)
{
    WaylandWindowData *windowData = (WaylandWindowData *)glfwGetWindowUserPointer(window);
    
    if (!windowData) return;
    
    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);
    
    for (int i = 0; i < windowData->scrollbarCount; i++) {
        WaylandScrollbar *scrollPtr = windowData->scrollbars[i];
        if (scrollPtr && scrollPtr->info.tkwin == windowData->tkwin) {
            int element = TkpScrollbarPosition((TkScrollbar *)scrollPtr, 
                                              (int)xpos, (int)ypos);
            
            if (element != OUTSIDE) {
                /* Handle wheel scrolling */
                if (scrollPtr->info.commandObj) {
                    Tcl_Obj *resultObj = Tcl_NewStringObj("scroll", -1);
                    
                    if (yoffset > 0) {
                        /* Scroll up/left */
                        Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj("-1", -1));
                    } else if (yoffset < 0) {
                        /* Scroll down/right */
                        Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj("1", -1));
                    }
                    
                    Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj("units", -1));
                    Tcl_IncrRefCount(resultObj);
                    
                    if (Tcl_EvalObjEx(scrollPtr->info.interp, resultObj, 0) != TCL_OK) {
                        /* Handle error */
                    }
                    
                    Tcl_DecrRefCount(resultObj);
                }
            }
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * WaylandScrollbar_AddToWindow --
 *
 *	Add a scrollbar to a window's scrollbar list.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Scrollbar is added to window data.
 *
 *----------------------------------------------------------------------
 */

static void
WaylandScrollbar_AddToWindow(
    GLFWwindow *glfwWindow,
    WaylandScrollbar *scrollbar)
{
    WaylandWindowData *windowData = (WaylandWindowData *)glfwGetWindowUserPointer(glfwWindow);
    
    if (!windowData) return;
    
    /* Reallocate scrollbar array */
    WaylandScrollbar **newScrollbars = (WaylandScrollbar **)Tcl_Realloc(
        windowData->scrollbars, 
        sizeof(WaylandScrollbar *) * (windowData->scrollbarCount + 1));
    
    if (!newScrollbars) return;
    
    windowData->scrollbars = newScrollbars;
    windowData->scrollbars[windowData->scrollbarCount] = scrollbar;
    windowData->scrollbarCount++;
}

/*
 *----------------------------------------------------------------------
 *
 * WaylandScrollbar_RemoveFromWindow --
 *
 *	Remove a scrollbar from a window's scrollbar list.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Scrollbar is removed from window data.
 *
 *----------------------------------------------------------------------
 */

static void
WaylandScrollbar_RemoveFromWindow(
    GLFWwindow *glfwWindow,
    WaylandScrollbar *scrollbar)
{
    WaylandWindowData *windowData = (WaylandWindowData *)glfwGetWindowUserPointer(glfwWindow);
    
    if (!windowData) return;
    
    for (int i = 0; i < windowData->scrollbarCount; i++) {
        if (windowData->scrollbars[i] == scrollbar) {
            /* Shift remaining elements */
            for (int j = i; j < windowData->scrollbarCount - 1; j++) {
                windowData->scrollbars[j] = windowData->scrollbars[j + 1];
            }
            windowData->scrollbarCount--;
            
            if (windowData->scrollbarCount == 0) {
                Tcl_Free(windowData->scrollbars);
                windowData->scrollbars = NULL;
            } else {
                WaylandScrollbar **newScrollbars = (WaylandScrollbar **)Tcl_Realloc(
                    windowData->scrollbars, 
                    sizeof(WaylandScrollbar *) * windowData->scrollbarCount);
                
                if (newScrollbars) {
                    windowData->scrollbars = newScrollbars;
                }
            }
            break;
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * GetGLFWWindowFromTkWindow --
 *
 *	Get the GLFW window associated with a Tk window.
 *	This is a stub that needs to be implemented by the windowing system.
 *
 * Results:
 *	Returns GLFW window handle or NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static GLFWwindow *
GetGLFWWindowFromTkWindow(
    Tk_Window tkwin)
{
    /* 
     * This needs to be implemented by the windowing system to map
     * Tk windows to GLFW windows. For now, return NULL.
     * In a real implementation, you might store the GLFW window
     * in the TkWindow structure or maintain a mapping table.
     */
    return NULL;
}

/*
 *--------------------------------------------------------------
 *
 * TkpDisplayScrollbar --
 *
 *	This procedure redraws the contents of a scrollbar window. It is
 *	invoked as a do-when-idle handler, so it only runs when there's
 *	nothing else for the application to do.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Information appears on the screen.
 *
 *--------------------------------------------------------------
 */

void
TkpDisplayScrollbar(
    void *clientData)	/* Information about window. */
{
    TkScrollbar *scrollPtr = (TkScrollbar *)clientData;
    Tk_Window tkwin = scrollPtr->tkwin;
    XPoint points[7];
    Tk_3DBorder border;
    int relief, width, elementBorderWidth;
    int borderWidth, highlightWidth;
    Pixmap pixmap;

    if ((scrollPtr->tkwin == NULL) || !Tk_IsMapped(tkwin)) {
	goto done;
    }

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
     * In order to avoid screen flashes, this procedure redraws the scrollbar
     * in a pixmap, then copies the pixmap to the screen in a single
     * operation. This means that there's no point in time where the on-sreen
     * image has been cleared.
     */

    pixmap = Tk_GetPixmap(scrollPtr->display, Tk_WindowId(tkwin),
	    Tk_Width(tkwin), Tk_Height(tkwin), Tk_Depth(tkwin));

    Tk_GetPixelsFromObj(NULL, scrollPtr->tkwin, scrollPtr->highlightWidthObj, &highlightWidth);
    if (highlightWidth > 0) {
	GC gc;

	if (scrollPtr->flags & GOT_FOCUS) {
	    gc = Tk_GCForColor(scrollPtr->highlightColorPtr, pixmap);
	} else {
	    gc = Tk_GCForColor(scrollPtr->highlightBgColorPtr, pixmap);
	}
	Tk_DrawFocusHighlight(tkwin, gc, highlightWidth, pixmap);
    }
    Tk_Draw3DRectangle(tkwin, pixmap, scrollPtr->bgBorder,
	    highlightWidth, highlightWidth,
	    Tk_Width(tkwin) - 2 * highlightWidth,
	    Tk_Height(tkwin) - 2 * highlightWidth,
	    borderWidth, scrollPtr->relief);
    XFillRectangle(scrollPtr->display, pixmap,
	    ((WaylandScrollbar*)scrollPtr)->troughGC,
	    scrollPtr->inset, scrollPtr->inset,
	    (unsigned) (Tk_Width(tkwin) - 2 * scrollPtr->inset),
	    (unsigned) (Tk_Height(tkwin) - 2 * scrollPtr->inset));

    /*
     * Draw the top or left arrow. The coordinates of the polygon points
     * probably seem odd, but they were carefully chosen with respect to X's
     * rules for filling polygons. These point choices cause the arrows to
     * just fill the narrow dimension of the scrollbar and be properly
     * centered.
     */

    if (scrollPtr->activeField == TOP_ARROW) {
	border = scrollPtr->activeBorder;
	relief = scrollPtr->activeField == TOP_ARROW ? scrollPtr->activeRelief
		: TK_RELIEF_RAISED;
    } else {
	border = scrollPtr->bgBorder;
	relief = TK_RELIEF_RAISED;
    }
    if (scrollPtr->vertical) {
	points[0].x = scrollPtr->inset - 1;
	points[0].y = scrollPtr->arrowLength + scrollPtr->inset - 1;
	points[1].x = width + scrollPtr->inset;
	points[1].y = points[0].y;
	points[2].x = width/2 + scrollPtr->inset;
	points[2].y = scrollPtr->inset - 1;
	Tk_Fill3DPolygon(tkwin, pixmap, border, points, 3,
		elementBorderWidth, relief);
    } else {
	points[0].x = scrollPtr->arrowLength + scrollPtr->inset - 1;
	points[0].y = scrollPtr->inset - 1;
	points[1].x = scrollPtr->inset;
	points[1].y = width/2 + scrollPtr->inset;
	points[2].x = points[0].x;
	points[2].y = width + scrollPtr->inset;
	Tk_Fill3DPolygon(tkwin, pixmap, border, points, 3,
		elementBorderWidth, relief);
    }

    /*
     * Display the bottom or right arrow.
     */

    if (scrollPtr->activeField == BOTTOM_ARROW) {
	border = scrollPtr->activeBorder;
	relief = scrollPtr->activeField == BOTTOM_ARROW
		? scrollPtr->activeRelief : TK_RELIEF_RAISED;
    } else {
	border = scrollPtr->bgBorder;
	relief = TK_RELIEF_RAISED;
    }
    if (scrollPtr->vertical) {
	points[0].x = scrollPtr->inset;
	points[0].y = Tk_Height(tkwin) - scrollPtr->arrowLength
		- scrollPtr->inset + 1;
	points[1].x = width/2 + scrollPtr->inset;
	points[1].y = Tk_Height(tkwin) - scrollPtr->inset;
	points[2].x = width + scrollPtr->inset;
	points[2].y = points[0].y;
	Tk_Fill3DPolygon(tkwin, pixmap, border,
		points, 3, elementBorderWidth, relief);
    } else {
	points[0].x = Tk_Width(tkwin) - scrollPtr->arrowLength
		- scrollPtr->inset + 1;
	points[0].y = scrollPtr->inset - 1;
	points[1].x = points[0].x;
	points[1].y = width + scrollPtr->inset;
	points[2].x = Tk_Width(tkwin) - scrollPtr->inset;
	points[2].y = width/2 + scrollPtr->inset;
	Tk_Fill3DPolygon(tkwin, pixmap, border,
		points, 3, elementBorderWidth, relief);
    }

    /*
     * Display the slider.
     */

    if (scrollPtr->activeField == SLIDER) {
	border = scrollPtr->activeBorder;
	relief = scrollPtr->activeField == SLIDER ? scrollPtr->activeRelief
		: TK_RELIEF_RAISED;
    } else {
	border = scrollPtr->bgBorder;
	relief = TK_RELIEF_RAISED;
    }
    if (scrollPtr->vertical) {
	Tk_Fill3DRectangle(tkwin, pixmap, border,
		scrollPtr->inset, scrollPtr->sliderFirst,
		width, scrollPtr->sliderLast - scrollPtr->sliderFirst,
		elementBorderWidth, relief);
    } else {
	Tk_Fill3DRectangle(tkwin, pixmap, border,
		scrollPtr->sliderFirst, scrollPtr->inset,
		scrollPtr->sliderLast - scrollPtr->sliderFirst, width,
		elementBorderWidth, relief);
    }

    /*
     * Copy the information from the off-screen pixmap onto the screen, then
     * delete the pixmap.
     */

    XCopyArea(scrollPtr->display, pixmap, Tk_WindowId(tkwin),
	    ((WaylandScrollbar*)scrollPtr)->copyGC, 0, 0,
	    (unsigned) Tk_Width(tkwin), (unsigned) Tk_Height(tkwin), 0, 0);
    Tk_FreePixmap(scrollPtr->display, pixmap);

  done:
    scrollPtr->flags &= ~REDRAW_PENDING;
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
 *	Frees the GCs associated with the scrollbar.
 *
 *----------------------------------------------------------------------
 */

void
TkpDestroyScrollbar(
    TkScrollbar *scrollPtr)
{
    WaylandScrollbar *waylandScrollPtr = (WaylandScrollbar *)scrollPtr;

    /* Remove scrollbar from window data */
    GLFWwindow *glfwWindow = GetGLFWWindowFromTkWindow(scrollPtr->tkwin);
    if (glfwWindow) {
        WaylandScrollbar_RemoveFromWindow(glfwWindow, waylandScrollPtr);
    }

    if (waylandScrollPtr->troughGC != NULL) {
	Tk_FreeGC(scrollPtr->display, waylandScrollPtr->troughGC);
    }
    if (waylandScrollPtr->copyGC != NULL) {
	Tk_FreeGC(scrollPtr->display, waylandScrollPtr->copyGC);
    }
    
    Tcl_Free((char *)waylandScrollPtr);
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
 *	Configuration info may get changed.
 *
 *----------------------------------------------------------------------
 */

void
TkpConfigureScrollbar(
    TkScrollbar *scrollPtr)
{
    XGCValues gcValues;
    GC newGC;
    WaylandScrollbar *waylandScrollPtr = (WaylandScrollbar *) scrollPtr;

    Tk_SetBackgroundFromBorder(scrollPtr->tkwin, scrollPtr->bgBorder);

    gcValues.foreground = scrollPtr->troughColorPtr->pixel;
    newGC = Tk_GetGC(scrollPtr->tkwin, GCForeground, &gcValues);
    if (waylandScrollPtr->troughGC != NULL) {
	Tk_FreeGC(scrollPtr->display, waylandScrollPtr->troughGC);
    }
    waylandScrollPtr->troughGC = newGC;
    if (waylandScrollPtr->copyGC == NULL) {
	gcValues.graphics_exposures = False;
	waylandScrollPtr->copyGC = Tk_GetGC(scrollPtr->tkwin,
		GCGraphicsExposures, &gcValues);
    }
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