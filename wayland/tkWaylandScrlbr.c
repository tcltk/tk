/*
 * tkWaylandScrollbar.c --
 *
 *	This file implements the wayland specific portion of the scrollbar
 *	widget.
 *
 * Copyright © 1996 Sun Microsystems, Inc
 * Copyright © 2001-2009 Apple Inc.
 * Copyright © 2006-2009 Daniel A. Steffen <das@users.sourceforge.net>
 * Copyright © 2018-2019 Marc Culler
 * Copyright © 2015-2026 Kevin Walzer
 
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkScrollbar.h"
#include "tkGlfwInt.h"

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
    int buttonDown;		/* Non-zero if mouse button is currently down. */
    int mouseOver;		/* Non-zero if mouse is currently over scrollbar. */
} WaylandScrollbar;

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
 * Forward declarations for internal functions
 */
static int ScrollbarEvent(TkScrollbar *scrollPtr, XEvent *eventPtr);
static void ScrollbarEventProc(void *clientData, XEvent *eventPtr);

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
    scrollPtr->buttonDown = 0;
    scrollPtr->mouseOver = 0;

    /* Register the platform-specific event handler. */
    Tk_CreateEventHandler(tkwin,
        ExposureMask|StructureNotifyMask|FocusChangeMask|
        EnterWindowMask|LeaveWindowMask|ButtonPressMask|ButtonReleaseMask,
        ScrollbarEventProc, (void *)scrollPtr);

    return (TkScrollbar *) scrollPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * ScrollbarEvent --
 *
 *	This procedure is invoked in response to <Button>,
 *      <ButtonRelease>, <EnterNotify>, and <LeaveNotify> events.  The
 *      Scrollbar appearance is modified for each event.
 *
 * Results:
 *      TCL_OK on success.
 *
 * Side effects:
 *      Scrollbar appearance may change.
 *
 *----------------------------------------------------------------------
 */

static int
ScrollbarEvent(
    TkScrollbar *scrollPtr,
    XEvent *eventPtr)
{
    WaylandScrollbar *wsPtr = (WaylandScrollbar *) scrollPtr;

    if (eventPtr->type == ButtonPress) {
        wsPtr->buttonDown = 1;
        int where = TkpScrollbarPosition(scrollPtr,
            eventPtr->xbutton.x, eventPtr->xbutton.y);
        
        /* Update active field based on where mouse was pressed. */
        scrollPtr->activeField = where;
        
        /* Handle different parts of the scrollbar. */
        switch (where) {
            case TOP_ARROW:
                /* Scroll up/left */
                if (scrollPtr->commandObj) {
                    Tcl_Obj *resultObj = Tcl_NewStringObj("scroll", -1);
                    Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj("-1", -1));
                    Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj("units", -1));
                    Tcl_IncrRefCount(resultObj);
                    
                    if (Tcl_EvalObjEx(scrollPtr->interp, resultObj, 0) != TCL_OK) {
                        /* Handle error. */
                    }
                    
                    Tcl_DecrRefCount(resultObj);
                }
                break;
                
            case BOTTOM_ARROW:
                /* Scroll down/right. */
                if (scrollPtr->commandObj) {
                    Tcl_Obj *resultObj = Tcl_NewStringObj("scroll", -1);
                    Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj("1", -1));
                    Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj("units", -1));
                    Tcl_IncrRefCount(resultObj);
                    
                    if (Tcl_EvalObjEx(scrollPtr->interp, resultObj, 0) != TCL_OK) {
                        /* Handle error. */
                    }
                    
                    Tcl_DecrRefCount(resultObj);
                }
                break;
                
            case TOP_GAP:
                /* Page up/left. */
                if (scrollPtr->commandObj) {
                    Tcl_Obj *resultObj = Tcl_NewStringObj("scroll", -1);
                    Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj("-1", -1));
                    Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj("pages", -1));
                    Tcl_IncrRefCount(resultObj);
                    
                    if (Tcl_EvalObjEx(scrollPtr->interp, resultObj, 0) != TCL_OK) {
                        /* Handle error */
                    }
                    
                    Tcl_DecrRefCount(resultObj);
                }
                break;
                
            case BOTTOM_GAP:
                /* Page down/right. */
                if (scrollPtr->commandObj) {
                    Tcl_Obj *resultObj = Tcl_NewStringObj("scroll", -1);
                    Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj("1", -1));
                    Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj("pages", -1));
                    Tcl_IncrRefCount(resultObj);
                    
                    if (Tcl_EvalObjEx(scrollPtr->interp, resultObj, 0) != TCL_OK) {
                        /* Handle error. */
                    }
                    
                    Tcl_DecrRefCount(resultObj);
                }
                break;
                
            case SLIDER:
                /* Start slider dragging (would need additional state). */
                break;
        }
    } else if (eventPtr->type == ButtonRelease) {
        wsPtr->buttonDown = 0;
        if (!wsPtr->mouseOver) {
            scrollPtr->activeField = OUTSIDE;
        }
    } else if (eventPtr->type == EnterNotify) {
        wsPtr->mouseOver = 1;
        if (!wsPtr->buttonDown) {
            /* Highlight the element under mouse. */
            int where = TkpScrollbarPosition(scrollPtr,
                eventPtr->xcrossing.x, eventPtr->xcrossing.y);
            scrollPtr->activeField = where;
        }
    } else if (eventPtr->type == LeaveNotify) {
        wsPtr->mouseOver = 0;
        if (!wsPtr->buttonDown) {
            scrollPtr->activeField = OUTSIDE;
        }
    }
    
    TkScrollbarEventuallyRedraw(scrollPtr);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * ScrollbarEventProc --
 *
 *	This procedure is invoked by the Tk dispatcher for various events on
 *	scrollbars.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	When the window gets deleted, internal structures get cleaned up. When
 *	it gets exposed, it is redisplayed.
 *
 *----------------------------------------------------------------------
 */

static void
ScrollbarEventProc(
    void *clientData,	/* Information about window. */
    XEvent *eventPtr)	/* Information about event. */
{
    TkScrollbar *scrollPtr = (TkScrollbar *)clientData;

    switch (eventPtr->type) {
    case ButtonPress:
    case ButtonRelease:
    case EnterNotify:
    case LeaveNotify:
        ScrollbarEvent(scrollPtr, eventPtr);
        break;
    default:
        /* Let the generic scrollbar handle other events. */
        TkScrollbarEventProc(scrollPtr, eventPtr);
    }
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
