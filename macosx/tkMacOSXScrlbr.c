/*
 * tkMacOSXScrollbar.c --
 *
 *	This file implements the Macintosh specific portion of the scrollbar
 *	widget.
 *
 * Copyright (c) 1996 by Sun Microsystems, Inc.
 * Copyright 2001-2009, Apple Inc.
 * Copyright (c) 2006-2009 Daniel A. Steffen <das@users.sourceforge.net>
 * Copyright (c) 2014 Marc Culler.
 * Copyright (c) 2014 Kevin Walzer/WordTech Commununications LLC.
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkScrollbar.h"
#include "tkMacOSXPrivate.h"


#define MIN_SCROLLBAR_VALUE		0
#define SCROLLBAR_SCALING_VALUE		((double)(LONG_MAX>>1))
#define MIN_SLIDER_LENGTH	5


/*
 * Declaration of Mac specific scrollbar structure.
 */

typedef struct MacScrollbar {
    TkScrollbar information;	 /* Generic scrollbar info. */
    GC troughGC;		/* For drawing trough. */
    GC copyGC;			/* Used for copying from pixmap onto screen. */ 
} MacScrollbar;

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


typedef struct ScrollbarMetrics {
    SInt32 width, minThumbHeight;
    int minHeight, topArrowHeight, bottomArrowHeight;
    NSControlSize controlSize;
} ScrollbarMetrics;

static ScrollbarMetrics metrics[2] = {
    {15, 54, 26, 14, 14, NSRegularControlSize}, /* kThemeScrollBarMedium */
    {11, 40, 20, 10, 10, NSSmallControlSize},  /* kThemeScrollBarSmall  */
};

HIThemeTrackDrawInfo info = {
    .version = 0,
    .min = 0.0,
    .max = 1.0,
    .attributes = kThemeTrackShowThumb,
    .kind = kThemeScrollBarMedium,
};


/*
 * This variable holds the default width for a scrollbar in string form for
 * use in a Tk_ConfigSpec.
 */

static char defWidth[TCL_INTEGER_SPACE];

/*
 * Forward declarations for procedures defined later in this file:
 */

static void ScrollbarEventProc(ClientData clientData, XEvent *eventPtr);
static void ScrollbarActionProc(ClientData clientData, ControlPartCode partCode);
static int ScrollbarPress(TkScrollbar *scrollPtr, XEvent *eventPtr);
static void UpdateControlValues(TkScrollbar  *scrollPtr);




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

     static int initialized = 0;
    MacScrollbar *scrollPtr = ckalloc(sizeof(MacScrollbar));

    scrollPtr->troughGC = None;
    scrollPtr->copyGC = None;
    TkWindow *winPtr = (TkWindow *)tkwin;

    Tk_CreateEventHandler(tkwin,ExposureMask|StructureNotifyMask|FocusChangeMask|ButtonPressMask|VisibilityChangeMask, ScrollbarEventProc, scrollPtr);
    
    return (TkScrollbar *) scrollPtr;
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
		    ClientData clientData)	/* Information about window. */
{
    register TkScrollbar *scrollPtr = (TkScrollbar *) clientData;
    register Tk_Window tkwin = scrollPtr->tkwin;


    if ((scrollPtr->tkwin == NULL) || !Tk_IsMapped(tkwin)) {
  	return;
    }

    MacScrollbar *macScrollPtr = clientData;
    TkWindow *winPtr = (TkWindow *) tkwin;
    MacDrawable *macWin =  (MacDrawable *) winPtr->window;
    TkMacOSXDrawingContext dc;
    NSView *view = TkMacOSXDrawableView(macWin);
    CGFloat viewHeight = [view bounds].size.height;
     CGAffineTransform t = { .a = 1, .b = 0, .c = 0, .d = -1, .tx = 0,
     	    .ty = viewHeight};

    
    scrollPtr->flags &= ~REDRAW_PENDING;
    if (!scrollPtr->tkwin || !Tk_IsMapped(tkwin) || !view ||
    	!TkMacOSXSetupDrawingContext((Drawable) macWin, NULL, 1, &dc)) {
    	return;
    }

     CGContextConcatCTM(dc.context, t);

    /*Draw Unix-style scroll trough to provide rect for native scrollbar.*/
    if (scrollPtr->highlightWidth != 0) {
    	GC fgGC, bgGC;

    	bgGC = Tk_GCForColor(scrollPtr->highlightBgColorPtr, (Pixmap) macWin);
    	if (scrollPtr->flags & GOT_FOCUS) {
    	    fgGC = Tk_GCForColor(scrollPtr->highlightColorPtr, (Pixmap) macWin);
    	} else {
    	    fgGC = bgGC;
    	}
    	TkpDrawHighlightBorder(tkwin, fgGC, bgGC, scrollPtr->highlightWidth,
    			       (Pixmap) macWin);
    }


    Tk_Draw3DRectangle(tkwin, (Pixmap) macWin, scrollPtr->bgBorder,
    		       scrollPtr->highlightWidth, scrollPtr->highlightWidth,
    		       Tk_Width(tkwin) - 2*scrollPtr->highlightWidth,
    		       Tk_Height(tkwin) - 2*scrollPtr->highlightWidth,
    		       scrollPtr->borderWidth, scrollPtr->relief);
    Tk_Fill3DRectangle(tkwin, (Pixmap) macWin, scrollPtr->bgBorder,
    		       scrollPtr->inset, scrollPtr->inset,
    		       Tk_Width(tkwin) - 2*scrollPtr->inset,
    		       Tk_Height(tkwin) - 2*scrollPtr->inset, 0, TK_RELIEF_FLAT);

    /*Update values and draw in native rect.*/
   
    UpdateControlValues(scrollPtr);
    HIThemeDrawTrack (&info, 0, dc.context, kHIThemeOrientationNormal);
    TkMacOSXRestoreDrawingContext(&dc);
    
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
    register TkScrollbar *scrollPtr)
				/* Scrollbar whose geometry may have
				 * changed. */
{

    int width, height, variant, fieldLength;

    if (scrollPtr->highlightWidth < 0) {
    	scrollPtr->highlightWidth = 0;
    }
    scrollPtr->inset = scrollPtr->highlightWidth + scrollPtr->borderWidth;
    variant = ((scrollPtr->vertical ? Tk_Width(scrollPtr->tkwin) :
    	    Tk_Height(scrollPtr->tkwin)) - 2 * scrollPtr->inset
    	    < metrics[0].width) ? 1 : 0;
    scrollPtr->arrowLength = (metrics[variant].topArrowHeight +
    	    metrics[variant].bottomArrowHeight) / 2;
    fieldLength = (scrollPtr->vertical ? Tk_Height(scrollPtr->tkwin)
    	    : Tk_Width(scrollPtr->tkwin))
    	    - 2 * (scrollPtr->arrowLength + scrollPtr->inset);
    if (fieldLength < 0) {
    	fieldLength = 0;
    }
    scrollPtr->sliderFirst = fieldLength * scrollPtr->firstFraction;
    scrollPtr->sliderLast = fieldLength * scrollPtr->lastFraction;

    /*
     * Adjust the slider so that some piece of it is always
     * displayed in the scrollbar and so that it has at least
     * a minimal width (so it can be grabbed with the mouse).
     */

    if (scrollPtr->sliderFirst > (fieldLength - 2*scrollPtr->borderWidth)) {
    	scrollPtr->sliderFirst = fieldLength - 2*scrollPtr->borderWidth;
    }
    if (scrollPtr->sliderFirst < 0) {
    	scrollPtr->sliderFirst = 0;
    }
    if (scrollPtr->sliderLast < (scrollPtr->sliderFirst +
    	    metrics[variant].minThumbHeight)) {
    	scrollPtr->sliderLast = scrollPtr->sliderFirst +
    		metrics[variant].minThumbHeight;
    }
    if (scrollPtr->sliderLast > fieldLength) {
    	scrollPtr->sliderLast = fieldLength;
    }
    scrollPtr->sliderFirst += scrollPtr->inset +
    	    metrics[variant].topArrowHeight;
    scrollPtr->sliderLast += scrollPtr->inset +
    	    metrics[variant].bottomArrowHeight;

    /*
     * Register the desired geometry for the window (leave enough space
     * for the two arrows plus a minimum-size slider, plus border around
     * the whole window, if any). Then arrange for the window to be
     * redisplayed.
     */

    if (scrollPtr->vertical) {
    	Tk_GeometryRequest(scrollPtr->tkwin, scrollPtr->width +
    		2 * scrollPtr->inset, 2 * (scrollPtr->arrowLength +
    		scrollPtr->borderWidth + scrollPtr->inset) +
    		metrics[variant].minThumbHeight);
    } else {
    	Tk_GeometryRequest(scrollPtr->tkwin, 2 * (scrollPtr->arrowLength +
    		scrollPtr->borderWidth + scrollPtr->inset) +
    		metrics[variant].minThumbHeight, scrollPtr->width +
    		2 * scrollPtr->inset);
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
     MacScrollbar *macScrollPtr = (MacScrollbar *)scrollPtr;

    if (macScrollPtr->troughGC != None) {
	Tk_FreeGC(scrollPtr->display, macScrollPtr->troughGC);
    }
    if (macScrollPtr->copyGC != None) {
	Tk_FreeGC(scrollPtr->display, macScrollPtr->copyGC);
    }

    macScrollPtr=NULL;
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
    register TkScrollbar *scrollPtr)
				/* Information about widget; may or may not
				 * already have values for some fields. */
{

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
		     register TkScrollbar *scrollPtr,
		     /* Scrollbar widget record. */
		     int x, int y)		/* Coordinates within scrollPtr's window. */
{

    int length, width, tmp, inset;
    inset = scrollPtr->inset;
    
    UpdateControlValues(scrollPtr);
  

    if ((x < scrollPtr->inset) || (x >= (Tk_Width(scrollPtr->tkwin) -
					 scrollPtr->inset)) || (y < scrollPtr->inset) ||
	(y >= (Tk_Height(scrollPtr->tkwin) - scrollPtr->inset))) {
	return OUTSIDE;
    }

    /*
     * All of the calculations in this procedure mirror those in
     * TkpDisplayScrollbar. Be sure to keep the two consistent.
     */

   /* Get the coordinates of the cursor and convered from Cocoa screen coordinates to Tk coordinates.*/
   NSPoint point = [NSEvent mouseLocation];
   float rootX = point.x;
   float rootY = point.y;
   float screenheight = [[[NSScreen screens] objectAtIndex:0] frame].size.height;
   float tk_Y  = screenheight - rootY;
   HIPoint where = {point.x, tk_Y};
  
   ControlPartCode partCode;
   ChkErr(HIThemeHitTestTrack, &info, &where, &partCode);

      switch (partCode) {
    case kAppearancePartUpButton:
	return TOP_ARROW;
    case kAppearancePartPageUpArea:
	return TOP_GAP;
    case kAppearancePartIndicator:
	return SLIDER;
    case kAppearancePartPageDownArea:
	return BOTTOM_GAP;
    case kAppearancePartDownButton:
	return BOTTOM_ARROW;
    default:
	return OUTSIDE;
    }

}

 /*
 *--------------------------------------------------------------
 *
 * UpdateControlValues --
 *
 *	This procedure updates the Macintosh scrollbar control to display the
 *	values defined by the Tk scrollbar.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The Macintosh control is updated.
 *
 *--------------------------------------------------------------
 */

static void
UpdateControlValues(
    TkScrollbar *scrollPtr)		/* Scrollbar data struct. */
{
 
    Tk_Window tkwin = scrollPtr->tkwin;
    MacDrawable *macWin = (MacDrawable *) Tk_WindowId(scrollPtr->tkwin);
    double dViewSize;
    HIRect  contrlRect;
    int variant, active; 
    short width, height;

    NSView *view = TkMacOSXDrawableView(macWin);
    CGFloat viewHeight = [view bounds].size.height;
    NSRect frame;
    frame = NSMakeRect(macWin->xOff, macWin->yOff, Tk_Width(tkwin),
		       Tk_Height(tkwin));
    frame = NSInsetRect(frame, scrollPtr->inset, scrollPtr->inset);
    frame.origin.y = viewHeight - (frame.origin.y + frame.size.height);

    contrlRect = NSRectToCGRect(frame);
    info.bounds = contrlRect;

    width = contrlRect.size.width;
    height = contrlRect.size.height;

    variant = contrlRect.size.width < metrics[0].width ? 1 : 0;
   
    /*
     * Ensure we set scrollbar control bounds only once all size adjustments
     * have been computed.
     */

    info.bounds = contrlRect;
    if (!scrollPtr->vertical) {
    	info.attributes |= kThemeTrackHorizontal;
    }
 
    /*
     * Given the Tk parameters for the fractions of the start and end of the
     * thumb, the following calculation determines the location for the
     * Macintosh thumb. The Aqua scroll control works as follows. The
     * scrollbar's value is the position of the left (or top) side of the view
     * area in the content area being scrolled. The maximum value of the
     * control is therefore the dimension of the content area less the size of
     * the view area.
     */

      dViewSize = scrollPtr->lastFraction - scrollPtr->firstFraction;
  
      if (!scrollPtr->vertical) {
	  info.trackInfo.scrollbar.viewsize = dViewSize * width;
	  info.value = scrollPtr->firstFraction * width;
      } else {
	  info.trackInfo.scrollbar.viewsize = dViewSize * height;
	  info.value = scrollPtr->firstFraction * height;
      }
      NSLog(@"firstfraction = %f, lastFraction = %f, value = %f, viewSize=%f", scrollPtr->firstFraction, scrollPtr->lastFraction, info.value, info.trackInfo.scrollbar.viewsize);
  
    if((scrollPtr->firstFraction <= 0.0 && scrollPtr->lastFraction >= 1.0)
       || height <= metrics[variant].minHeight) {
    	info.enableState = kThemeTrackHideTrack;
    } else {
    	info.enableState = kThemeTrackActive;
    	info.attributes = kThemeTrackShowThumb |  kThemeTrackThumbRgnIsNotGhost;;
    }

}


/*
 *--------------------------------------------------------------
 *
 * ScrollbarActionProc --
 *
 *	Callback procedure used by the Macintosh toolbox call
 *	HandleControlClick. This call will update the display while the
 *	scrollbar is being manipulated by the user.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May change the display.
 *
 *--------------------------------------------------------------
 */

static void
ScrollbarActionProc(
		    ClientData clientData,
		    ControlPartCode partCode
		    )
{
    TkScrollbar *scrollPtr = clientData;
 
    Tcl_DString cmdString;

    /* Get the coordinates of the cursor and convered from Cocoa screen coordinates to Tk coordinates.*/
    NSPoint point = [NSEvent mouseLocation];
    float rootX = point.x;
    float rootY = point.y;
    float screenheight = [[[NSScreen screens] objectAtIndex:0] frame].size.height;
    float tk_Y  = screenheight - rootY;
    HIPoint where = {point.x, tk_Y};
  
    ChkErr(HIThemeHitTestTrack, &info, &where, &partCode);
    Tcl_Interp *interp;
    interp = scrollPtr->interp;

    Tcl_DStringInit(&cmdString);
    Tcl_DStringAppend(&cmdString, scrollPtr->command,
		      scrollPtr->commandSize);

    if ( partCode == kAppearancePartUpButton ||
	 partCode == kAppearancePartDownButton ) {
	Tcl_DStringAppendElement(&cmdString, "scroll");
	Tcl_DStringAppendElement(&cmdString,
				 (partCode == kAppearancePartUpButton) ? "-1" : "1");
	Tcl_DStringAppendElement(&cmdString, "unit");
    } else if (partCode == kAppearancePartPageUpArea ||
	       partCode == kAppearancePartPageDownArea ) {
	Tcl_DStringAppendElement(&cmdString, "scroll");
	Tcl_DStringAppendElement(&cmdString,
				 (partCode == kAppearancePartPageUpArea) ? "-1" : "1");
	Tcl_DStringAppendElement(&cmdString, "page");
    } else if (partCode == kAppearancePartIndicator) {
	char valueString[TCL_DOUBLE_SPACE];
	Tcl_PrintDouble(NULL, info.value -
			MIN_SCROLLBAR_VALUE / SCROLLBAR_SCALING_VALUE, valueString);
	Tcl_DStringAppendElement(&cmdString, "moveto");
	Tcl_DStringAppendElement(&cmdString, valueString);
    } 
    Tcl_Preserve(scrollPtr->interp);
    Tcl_EvalEx(scrollPtr->interp, Tcl_DStringValue(&cmdString),
	       Tcl_DStringLength(&cmdString), TCL_EVAL_GLOBAL);
    Tcl_Release(scrollPtr->interp);
    Tcl_DStringFree(&cmdString);
    	
}
/*
 *--------------------------------------------------------------
 *
 * ScrollbarPress --
 *
 *	This procedure is invoked in response to <ButtonPress> events.
 *	Enters a modal loop to handle scrollbar interactions.
 *
 *--------------------------------------------------------------
 */

static int
ScrollbarPress(TkScrollbar *scrollPtr, XEvent *eventPtr)
{
  
    Window window;

    if (eventPtr->type == ButtonPress) {

	/* Get the coordinates of the cursor and convered from Cocoa screen coordinates to Tk coordinates.*/
	NSPoint point = [NSEvent mouseLocation];
	float rootX = point.x;
	float rootY = point.y;
	float screenheight = [[[NSScreen screens] objectAtIndex:0] frame].size.height;
	float tk_Y  = screenheight - rootY;
	HIPoint where = {point.x, tk_Y};
  
	ControlPartCode partCode;
	ChkErr(HIThemeHitTestTrack, &info, &where, &partCode);

	ScrollbarActionProc(scrollPtr, partCode);
    	/*
    	 * This call will "eat" the ButtonUp event. We now
    	 * generate a ButtonUp event so Tk will unset implicit grabs etc.
    	 */

    	if (scrollPtr->tkwin) {
    	    window = Tk_WindowId(scrollPtr->tkwin);
    	    TkGenerateButtonEventForXPointer(window);
    	}

	return TCL_OK;

    }
}



/*
 *--------------------------------------------------------------
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
 *--------------------------------------------------------------
 */

static void
ScrollbarEventProc(
    ClientData clientData,	/* Information about window. */
    XEvent *eventPtr)		/* Information about event. */
{
    TkScrollbar *scrollPtr = clientData;

    switch (eventPtr->type) {
    case UnmapNotify:
	TkMacOSXSetScrollbarGrow((TkWindow *) scrollPtr->tkwin, false);
	break;
    case ActivateNotify:
    case DeactivateNotify:
	TkScrollbarEventuallyRedraw(scrollPtr);
	break;
    case ButtonPress:
    	ScrollbarPress(clientData, eventPtr);
	break;
    default:
	TkScrollbarEventProc(clientData, eventPtr);
    }
}



/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
