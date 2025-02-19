/*
 * tkSquare.c --
 *
 *	This module implements "square" widgets that are object based. A
 *	"square" is a widget that displays a single square that can be moved
 *	around and resized. This file is intended as an example of how to
 *	build a widget; it isn't included in the normal wish, but it is
 *	included in "tktest".
 *
 * Copyright © 1997 Sun Microsystems, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#if 0
#define __NO_OLD_CONFIG
#endif
#ifndef USE_TCL_STUBS
#   define USE_TCL_STUBS
#endif
#ifndef USE_TK_STUBS
#   define USE_TK_STUBS
#endif
#include "tkInt.h"
#include <stdbool.h>

/*
 * A data structure of the following type is kept for each square widget
 * managed by this file:
 */

typedef struct {
    Tk_Window tkwin;		/* Window that embodies the square. NULL means
				 * window has been deleted but widget record
				 * hasn't been cleaned up yet. */
    Display *display;		/* X's token for the window's display. */
    Tcl_Interp *interp;		/* Interpreter associated with widget. */
    Tcl_Command widgetCmd;	/* Token for square's widget command. */
    Tk_OptionTable optionTable;	/* Token representing the configuration
				 * specifications. */
    Tcl_Obj *xPtr, *yPtr;	/* Position of square's upper-left corner
				 * within widget. */
    int x, y;
    Tcl_Obj *sizeObjPtr;	/* Width and height of square. */

    /*
     * Information used when displaying widget:
     */

    Tcl_Obj *borderWidthObj;	/* Width of 3-D border around whole widget. */
    Tcl_Obj *bgBorderPtr;
    Tcl_Obj *fgBorderPtr;
    Tcl_Obj *reliefPtr;
    GC gc;			/* Graphics context for copying from
				 * off-screen pixmap onto screen. */
    bool doubleBuffer;	/* true means double-buffer redisplay with
				 * pixmap; false means draw straight onto the
				 * display. */
    bool updatePending;		/* true means a call to SquareDisplay has
				 * already been scheduled. */
} Square;

/*
 * Information used for argv parsing.
 */

static const Tk_OptionSpec optionSpecs[] = {
    {TK_OPTION_BORDER, "-background", "background", "Background",
	    "#d9d9d9", offsetof(Square, bgBorderPtr), TCL_INDEX_NONE, 0,
	    "white", 0},
    {TK_OPTION_SYNONYM, "-bd", NULL, NULL, NULL, 0, TCL_INDEX_NONE, 0,
	    "-borderwidth", 0},
    {TK_OPTION_SYNONYM, "-bg", NULL, NULL, NULL, 0, TCL_INDEX_NONE, 0,
	    "-background", 0},
    {TK_OPTION_PIXELS, "-borderwidth", "borderWidth", "BorderWidth",
	    "2", offsetof(Square, borderWidthObj), TCL_INDEX_NONE, 0, NULL, 0},
    {TK_OPTION_BOOLEAN, "-dbl", "doubleBuffer", "DoubleBuffer",
	    "1", TCL_INDEX_NONE, offsetof(Square, doubleBuffer), TK_OPTION_VAR(bool) , NULL, 0},
    {TK_OPTION_SYNONYM, "-fg", NULL, NULL, NULL, 0, TCL_INDEX_NONE, 0,
	    "-foreground", 0},
    {TK_OPTION_BORDER, "-foreground", "foreground", "Foreground",
	    "#b03060", offsetof(Square, fgBorderPtr), TCL_INDEX_NONE, 0,
	    "black", 0},
    {TK_OPTION_PIXELS, "-posx", "posx", "PosX", "0",
	    offsetof(Square, xPtr), TCL_INDEX_NONE, 0, NULL, 0},
    {TK_OPTION_PIXELS, "-posy", "posy", "PosY", "0",
	    offsetof(Square, yPtr), TCL_INDEX_NONE, 0, NULL, 0},
    {TK_OPTION_RELIEF, "-relief", "relief", "Relief",
	    "raised", offsetof(Square, reliefPtr), TCL_INDEX_NONE, 0, NULL, 0},
    {TK_OPTION_PIXELS, "-size", "size", "Size", "20",
	    offsetof(Square, sizeObjPtr), TCL_INDEX_NONE, 0, NULL, 0},
    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0}
};

/*
 * Forward declarations for procedures defined later in this file:
 */

static void		SquareDeletedProc(void *clientData);
static int		SquareConfigure(Tcl_Interp *interp, Square *squarePtr);
static void		SquareDisplay(void *clientData);
static void		KeepInWindow(Square *squarePtr);
static void		SquareObjEventProc(void *clientData,
			    XEvent *eventPtr);
static Tcl_ObjCmdProc SquareWidgetObjCmd;

/*
 *--------------------------------------------------------------
 *
 * SquareCmd --
 *
 *	This procedure is invoked to process the "square" Tcl command. It
 *	creates a new "square" widget.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	A new widget is created and configured.
 *
 *--------------------------------------------------------------
 */

int
SquareObjCmd(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,		/* Current interpreter. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    Square *squarePtr;
    Tk_Window tkwin;
    Tk_OptionTable optionTable;

    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "pathName ?-option value ...?");
	return TCL_ERROR;
    }

    tkwin = Tk_CreateWindowFromPath(interp, Tk_MainWindow(interp),
	    Tcl_GetString(objv[1]), NULL);
    if (tkwin == NULL) {
	return TCL_ERROR;
    }
    Tk_SetClass(tkwin, "Square");

    /*
     * Create the option table for this widget class. If it has already been
     * created, the refcount will get bumped and just the pointer will be
     * returned. The refcount getting bumped does not concern us, because Tk
     * will ensure the table is deleted when the interpreter is destroyed.
     */

    optionTable = Tk_CreateOptionTable(interp, optionSpecs);

    /*
     * Allocate and initialize the widget record. The memset allows us to set
     * just the non-NULL/0 items.
     */

    squarePtr = (Square *)ckalloc(sizeof(Square));
    memset(squarePtr, 0, sizeof(Square));

    squarePtr->tkwin = tkwin;
    squarePtr->display = Tk_Display(tkwin);
    squarePtr->interp = interp;
    squarePtr->widgetCmd = Tcl_CreateObjCommand(interp,
	    Tk_PathName(squarePtr->tkwin), SquareWidgetObjCmd, squarePtr,
	    SquareDeletedProc);
    squarePtr->gc = NULL;
    squarePtr->optionTable = optionTable;

    if (Tk_InitOptions(interp, squarePtr, optionTable, tkwin)
	    != TCL_OK) {
	Tk_DestroyWindow(squarePtr->tkwin);
	ckfree(squarePtr);
	return TCL_ERROR;
    }

    Tk_CreateEventHandler(squarePtr->tkwin, ExposureMask|StructureNotifyMask,
	    SquareObjEventProc, squarePtr);
    if (Tk_SetOptions(interp, squarePtr, optionTable, objc - 2,
	    objv + 2, tkwin, NULL, NULL) != TCL_OK) {
	goto error;
    }
    if (SquareConfigure(interp, squarePtr) != TCL_OK) {
	goto error;
    }

    Tcl_SetObjResult(interp,
	    Tcl_NewStringObj(Tk_PathName(squarePtr->tkwin), TCL_INDEX_NONE));
    return TCL_OK;

  error:
    Tk_DestroyWindow(squarePtr->tkwin);
    return TCL_ERROR;
}

/*
 *--------------------------------------------------------------
 *
 * SquareWidgetObjCmd --
 *
 *	This procedure is invoked to process the Tcl command that corresponds
 *	to a widget managed by this module. See the user documentation for
 *	details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *--------------------------------------------------------------
 */

static int
SquareWidgetObjCmd(
    void *clientData,	/* Information about square widget. */
    Tcl_Interp *interp,		/* Current interpreter. */
    int objc,			/* Number of arguments. */
    Tcl_Obj * const objv[])	/* Argument objects. */
{
    Square *squarePtr = (Square *)clientData;
    int result = TCL_OK;
    static const char *const squareOptions[] = {"cget", "configure", NULL};
    enum {
	SQUARE_CGET, SQUARE_CONFIGURE
    };
    Tcl_Obj *resultObjPtr;
    int index;

    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "option ?arg ...?");
	return TCL_ERROR;
    }

    if (Tcl_GetIndexFromObjStruct(interp, objv[1], squareOptions,
	    sizeof(char *), "command", 0, &index) != TCL_OK) {
	return TCL_ERROR;
    }

    Tcl_Preserve(squarePtr);

    switch (index) {
    case SQUARE_CGET:
	if (objc != 3) {
	    Tcl_WrongNumArgs(interp, 2, objv, "option");
	    goto error;
	}
	resultObjPtr = Tk_GetOptionValue(interp, squarePtr,
		squarePtr->optionTable, objv[2], squarePtr->tkwin);
	if (resultObjPtr == NULL) {
	    result = TCL_ERROR;
	} else {
	    Tcl_SetObjResult(interp, resultObjPtr);
	}
	break;
    case SQUARE_CONFIGURE:
	resultObjPtr = NULL;
	if (objc == 2) {
	    resultObjPtr = Tk_GetOptionInfo(interp, squarePtr,
		    squarePtr->optionTable, NULL, squarePtr->tkwin);
	    if (resultObjPtr == NULL) {
		result = TCL_ERROR;
	    }
	} else if (objc == 3) {
	    resultObjPtr = Tk_GetOptionInfo(interp, squarePtr,
		    squarePtr->optionTable, objv[2], squarePtr->tkwin);
	    if (resultObjPtr == NULL) {
		result = TCL_ERROR;
	    }
	} else {
	    result = Tk_SetOptions(interp, squarePtr,
		    squarePtr->optionTable, objc - 2, objv + 2,
		    squarePtr->tkwin, NULL, NULL);
	    if (result == TCL_OK) {
		result = SquareConfigure(interp, squarePtr);
	    }
	    if (!squarePtr->updatePending) {
		Tcl_DoWhenIdle(SquareDisplay, squarePtr);
		squarePtr->updatePending = true;
	    }
	}
	if (resultObjPtr != NULL) {
	    Tcl_SetObjResult(interp, resultObjPtr);
	}
    }
    Tcl_Release(squarePtr);
    return result;

  error:
    Tcl_Release(squarePtr);
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * SquareConfigure --
 *
 *	This procedure is called to process an objv/objc list in conjunction
 *	with the Tk option database to configure (or reconfigure) a square
 *	widget.
 *
 * Results:
 *	The return value is a standard Tcl result. If TCL_ERROR is returned,
 *	then the interp's result contains an error message.
 *
 * Side effects:
 *	Configuration information, such as colors, border width, etc. get set
 *	for squarePtr; old resources get freed, if there were any.
 *
 *----------------------------------------------------------------------
 */

static int
SquareConfigure(
    TCL_UNUSED(Tcl_Interp *),		/* Used for error reporting. */
    Square *squarePtr)		/* Information about widget. */
{
    int borderWidth;
    Tk_3DBorder bgBorder;

    /*
     * Set the background for the window and create a graphics context for use
     * during redisplay.
     */

    bgBorder = Tk_Get3DBorderFromObj(squarePtr->tkwin,
	    squarePtr->bgBorderPtr);
    Tk_SetWindowBackground(squarePtr->tkwin,
	    Tk_3DBorderColor(bgBorder)->pixel);
    if ((squarePtr->gc == NULL) && squarePtr->doubleBuffer) {
	XGCValues gcValues;
	gcValues.function = GXcopy;
	gcValues.graphics_exposures = False;
	squarePtr->gc = Tk_GetGC(squarePtr->tkwin,
		GCFunction|GCGraphicsExposures, &gcValues);
    }

    /*
     * Register the desired geometry for the window. Then arrange for the
     * window to be redisplayed.
     */

    Tk_GeometryRequest(squarePtr->tkwin, 200, 150);
    Tk_GetPixelsFromObj(NULL, squarePtr->tkwin, squarePtr->borderWidthObj,
	    &borderWidth);
    Tk_SetInternalBorder(squarePtr->tkwin, borderWidth);
    if (!squarePtr->updatePending) {
	Tcl_DoWhenIdle(SquareDisplay, squarePtr);
	squarePtr->updatePending = true;
    }
    KeepInWindow(squarePtr);
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * SquareObjEventProc --
 *
 *	This procedure is invoked by the Tk dispatcher for various events on
 *	squares.
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
SquareObjEventProc(
    void *clientData,	/* Information about window. */
    XEvent *eventPtr)		/* Information about event. */
{
    Square *squarePtr = (Square *)clientData;

    if (eventPtr->type == Expose) {
	if (!squarePtr->updatePending) {
	    Tcl_DoWhenIdle(SquareDisplay, squarePtr);
	    squarePtr->updatePending = true;
	}
    } else if (eventPtr->type == ConfigureNotify) {
	KeepInWindow(squarePtr);
	if (!squarePtr->updatePending) {
	    Tcl_DoWhenIdle(SquareDisplay, squarePtr);
	    squarePtr->updatePending = true;
	}
    } else if (eventPtr->type == DestroyNotify) {
	if (squarePtr->tkwin != NULL) {
	    Tk_FreeConfigOptions(squarePtr, squarePtr->optionTable,
		    squarePtr->tkwin);
	    if (squarePtr->gc != NULL) {
		Tk_FreeGC(squarePtr->display, squarePtr->gc);
	    }
	    squarePtr->tkwin = NULL;
	    Tcl_DeleteCommandFromToken(squarePtr->interp,
		    squarePtr->widgetCmd);
	}
	if (squarePtr->updatePending) {
	    Tcl_CancelIdleCall(SquareDisplay, squarePtr);
	}
	Tcl_EventuallyFree(squarePtr, TCL_DYNAMIC);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * SquareDeletedProc --
 *
 *	This procedure is invoked when a widget command is deleted. If the
 *	widget isn't already in the process of being destroyed, this command
 *	destroys it.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The widget is destroyed.
 *
 *----------------------------------------------------------------------
 */

static void
SquareDeletedProc(
    void *clientData)	/* Pointer to widget record for widget. */
{
    Square *squarePtr = (Square *)clientData;
    Tk_Window tkwin = squarePtr->tkwin;

    /*
     * This procedure could be invoked either because the window was destroyed
     * and the command was then deleted (in which case tkwin is NULL) or
     * because the command was deleted, and then this procedure destroys the
     * widget.
     */

    if (tkwin != NULL) {
	Tk_DestroyWindow(tkwin);
    }
}

/*
 *--------------------------------------------------------------
 *
 * SquareDisplay --
 *
 *	This procedure redraws the contents of a square window. It is invoked
 *	as a do-when-idle handler, so it only runs when there's nothing else
 *	for the application to do.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Information appears on the screen.
 *
 *--------------------------------------------------------------
 */

static void
SquareDisplay(
    void *clientData)	/* Information about window. */
{
    Square *squarePtr = (Square *)clientData;
    Tk_Window tkwin = squarePtr->tkwin;
    Pixmap pm = None;
    Drawable d;
    int borderWidth, size, relief;
    Tk_3DBorder bgBorder, fgBorder;

    squarePtr->updatePending = false;
    if (!Tk_IsMapped(tkwin)) {
	return;
    }

    /*
     * Create a pixmap for double-buffering, if necessary.
     */

    if (squarePtr->doubleBuffer) {
	pm = Tk_GetPixmap(Tk_Display(tkwin), Tk_WindowId(tkwin),
		Tk_Width(tkwin), Tk_Height(tkwin),
		DefaultDepthOfScreen(Tk_Screen(tkwin)));
	d = pm;
    } else {
	d = Tk_WindowId(tkwin);
    }

    /*
     * Redraw the widget's background and border.
     */

    Tk_GetPixelsFromObj(NULL, squarePtr->tkwin, squarePtr->borderWidthObj,
	    &borderWidth);
    bgBorder = Tk_Get3DBorderFromObj(squarePtr->tkwin,
	    squarePtr->bgBorderPtr);
    Tk_GetReliefFromObj(NULL, squarePtr->reliefPtr, &relief);
    Tk_Fill3DRectangle(tkwin, d, bgBorder, 0, 0, Tk_Width(tkwin),
	    Tk_Height(tkwin), borderWidth, relief);

    /*
     * Display the square.
     */

    Tk_GetPixelsFromObj(NULL, squarePtr->tkwin, squarePtr->sizeObjPtr, &size);
    fgBorder = Tk_Get3DBorderFromObj(squarePtr->tkwin,
	    squarePtr->fgBorderPtr);
    Tk_Fill3DRectangle(tkwin, d, fgBorder, squarePtr->x, squarePtr->y, size,
	    size, borderWidth, TK_RELIEF_RAISED);

    /*
     * If double-buffered, copy to the screen and release the pixmap.
     */

    if (squarePtr->doubleBuffer) {
	XCopyArea(Tk_Display(tkwin), pm, Tk_WindowId(tkwin), squarePtr->gc,
		0, 0, (unsigned) Tk_Width(tkwin), (unsigned) Tk_Height(tkwin),
		0, 0);
	Tk_FreePixmap(Tk_Display(tkwin), pm);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * KeepInWindow --
 *
 *	Adjust the position of the square if necessary to keep it in the
 *	widget's window.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The x and y position of the square are adjusted if necessary to keep
 *	the square in the window.
 *
 *----------------------------------------------------------------------
 */

static void
KeepInWindow(
    Square *squarePtr)	/* Pointer to widget record. */
{
    int i, bd, relief;
    int borderWidth, size;

    Tk_GetPixelsFromObj(NULL, squarePtr->tkwin, squarePtr->borderWidthObj,
	    &borderWidth);
    Tk_GetPixelsFromObj(NULL, squarePtr->tkwin, squarePtr->xPtr,
	    &squarePtr->x);
    Tk_GetPixelsFromObj(NULL, squarePtr->tkwin, squarePtr->yPtr,
	    &squarePtr->y);
    Tk_GetPixelsFromObj(NULL, squarePtr->tkwin, squarePtr->sizeObjPtr, &size);
    Tk_GetReliefFromObj(NULL, squarePtr->reliefPtr, &relief);
    bd = 0;
    if (relief != TK_RELIEF_FLAT) {
	bd = borderWidth;
    }
    i = (Tk_Width(squarePtr->tkwin) - bd) - (squarePtr->x + size);
    if (i < 0) {
	squarePtr->x += i;
    }
    i = (Tk_Height(squarePtr->tkwin) - bd) - (squarePtr->y + size);
    if (i < 0) {
	squarePtr->y += i;
    }
    if (squarePtr->x < bd) {
	squarePtr->x = bd;
    }
    if (squarePtr->y < bd) {
	squarePtr->y = bd;
    }
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
