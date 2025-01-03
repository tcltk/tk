/*
 * tkScrollbar.h --
 *
 *	Declarations of types and functions used to implement the scrollbar
 *	widget.
 *
 * Copyright © 1996 Sun Microsystems, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef _TKSCROLLBAR
#define _TKSCROLLBAR

#ifndef _TKINT
#include "tkInt.h"
#endif

/*
 * A data structure of the following type is kept for each scrollbar widget.
 */

typedef struct TkScrollbar {
    Tk_Window tkwin;		/* Window that embodies the scrollbar. NULL
				 * means that the window has been destroyed
				 * but the data structures haven't yet been
				 * cleaned up.*/
    Display *display;		/* Display containing widget. Used, among
				 * other things, so that resources can be
				 * freed even after tkwin has gone away. */
    Tcl_Interp *interp;		/* Interpreter associated with scrollbar. */
    Tcl_Command widgetCmd;	/* Token for scrollbar's widget command. */
    int vertical;		/* Non-zero means vertical orientation
				 * requested, zero means horizontal. */
#if TK_MAJOR_VERSION > 8
    Tcl_Obj *widthObj;		/* Desired narrow dimension of scrollbar, in
				 * pixels. */
#else
    int width;			/* Desired narrow dimension of scrollbar, in
				 * pixels. */
#endif
    Tcl_Obj *commandObj;		/* Command prefix to use when invoking
				 * scrolling commands. NULL means don't invoke
				 * commands. */
    int repeatDelay;		/* How long to wait before auto-repeating on
				 * scrolling actions (in ms). */
    int repeatInterval;		/* Interval between autorepeats (in ms). */
    int jump;			/* Value of -jump option. */

    /*
     * Information used when displaying widget:
     */

#if TK_MAJOR_VERSION > 8
    Tcl_Obj *borderWidthObj;	/* Width of 3-D borders. */
#else
    int borderWidth;
#endif
    Tk_3DBorder bgBorder;	/* Used for drawing background (all flat
				 * surfaces except for trough). */
    Tk_3DBorder activeBorder;	/* For drawing backgrounds when active (i.e.
				 * when mouse is positioned over element). */
    XColor *troughColorPtr;	/* Color for drawing trough. */
    int relief;			/* Indicates whether window as a whole is
				 * raised, sunken, or flat. */
#if TK_MAJOR_VERSION > 8
    Tcl_Obj *highlightWidthObj;	/* Width in pixels of highlight to draw around
				 * widget when it has the focus. <= 0 means
				 * don't draw a highlight. */
#else
    int highlightWidth;
#endif
    XColor *highlightBgColorPtr;
				/* Color for drawing traversal highlight area
				 * when highlight is off. */
    XColor *highlightColorPtr;	/* Color for drawing traversal highlight. */
    int inset;			/* Total width of all borders, including
				 * traversal highlight and 3-D border.
				 * Indicates how much interior stuff must be
				 * offset from outside edges to leave room for
				 * borders. */
#if TK_MAJOR_VERSION > 8
    Tcl_Obj *elementBorderWidthObj;	/* Width of border to draw around elements
				 * inside scrollbar (arrows and slider). -1
				 * means use borderWidth. */
#else
    int elementBorderWidth;
#endif
    int arrowLength;		/* Length of arrows along long dimension of
				 * scrollbar, including space for a small gap
				 * between the arrow and the slider.
				 * Recomputed on window size changes. */
    int sliderFirst;		/* Pixel coordinate of top or left edge of
				 * slider area, including border. */
    int sliderLast;		/* Coordinate of pixel just after bottom or
				 * right edge of slider area, including
				 * border. */
    int activeField;		/* Names field to be displayed in active
				 * colors, such as TOP_ARROW, or 0 for no
				 * field. */
    int activeRelief;		/* Value of -activeRelief option: relief to
				 * use for active element. */

    /*
     * Information describing the application related to the scrollbar, which
     * is provided by the application by invoking the "set" widget command.
     */

#if TK_MAJOR_VERSION < 9
    int dummy1, dummy2, dummy3, dummy4; /* deprecated, for "old" form. */
#endif
    double firstFraction;	/* Position of first visible thing in window,
				 * specified as a fraction between 0 and 1.0. */
    double lastFraction;	/* Position of last visible thing in window,
				 * specified as a fraction between 0 and 1.0. */

    /*
     * Miscellaneous information:
     */

    Tk_Cursor cursor;		/* Current cursor for window, or NULL. */
    Tcl_Obj *takeFocusObj;		/* Value of -takefocus option; not used in the
				 * C code, but used by keyboard traversal
				 * scripts. May be NULL. */
    int flags;			/* Various flags; see below for
				 * definitions. */
} TkScrollbar;

/*
 * Legal values for "activeField" field of Scrollbar structures. These are
 * also the return values from the ScrollbarPosition function.
 */

#define OUTSIDE		0
#define TOP_ARROW	1
#define TOP_GAP		2
#define SLIDER		3
#define BOTTOM_GAP	4
#define BOTTOM_ARROW	5

/*
 * Flag bits for scrollbars:
 *
 * REDRAW_PENDING:		Non-zero means a DoWhenIdle handler has
 *				already been queued to redraw this window.
 * GOT_FOCUS:			Non-zero means this window has the input
 *				focus.
 */

#define REDRAW_PENDING		1
#define GOT_FOCUS		4

/*
 * Declaration of scrollbar class functions structure
 * and default scrollbar width, for use in configSpec.
 */

MODULE_SCOPE const Tk_ClassProcs tkpScrollbarProcs;
MODULE_SCOPE char tkDefScrollbarWidth[TCL_INTEGER_SPACE];

/*
 * Declaration of functions used in the implementation of the scrollbar
 * widget.
 */

MODULE_SCOPE void	TkScrollbarEventProc(void *clientData,
			    XEvent *eventPtr);
MODULE_SCOPE void	TkScrollbarEventuallyRedraw(TkScrollbar *scrollPtr);
MODULE_SCOPE void	TkpComputeScrollbarGeometry(TkScrollbar *scrollPtr);
MODULE_SCOPE TkScrollbar *TkpCreateScrollbar(Tk_Window tkwin);
MODULE_SCOPE void	TkpDestroyScrollbar(TkScrollbar *scrollPtr);
MODULE_SCOPE void	TkpDisplayScrollbar(void *clientData);
MODULE_SCOPE void	TkpConfigureScrollbar(TkScrollbar *scrollPtr);
MODULE_SCOPE int	TkpScrollbarPosition(TkScrollbar *scrollPtr,
			    int x, int y);

#endif /* _TKSCROLLBAR */
