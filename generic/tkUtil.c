/*
 * tkUtil.c --
 *
 *	This file contains miscellaneous utility functions that are used by
 *	the rest of Tk, such as a function for drawing a focus highlight.
 *
 * Copyright © 1994 The Regents of the University of California.
 * Copyright © 1994-1997 Sun Microsystems, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"

#ifdef _WIN32
#include "tkWinInt.h"
#endif

/*
 * The structure below defines the implementation of the "statekey" Tcl
 * object, used for quickly finding a mapping in a TkStateMap.
 */

const TkObjType tkStateKeyObjType = {
    {"statekey",		/* name */
    NULL,			/* freeIntRepProc */
    NULL,			/* dupIntRepProc */
    NULL,			/* updateStringProc */
    NULL,			/* setFromAnyProc */
    TCL_OBJTYPE_V0},
    0
};

/*
 *--------------------------------------------------------------
 *
 * TkStateParseProc --
 *
 *	This function is invoked during option processing to handle the
 *	"-state" and "-default" options.
 *
 * Results:
 *	A standard Tcl return value.
 *
 * Side effects:
 *	The state for a given item gets replaced by the state indicated in the
 *	value argument.
 *
 *--------------------------------------------------------------
 */

int
TkStateParseProc(
    void *clientData,	/* some flags.*/
    Tcl_Interp *interp,		/* Used for reporting errors. */
    TCL_UNUSED(Tk_Window),		/* Window containing canvas widget. */
    const char *value,		/* Value of option. */
    char *widgRec,		/* Pointer to record for item. */
    Tcl_Size offset)			/* Offset into item. */
{
    int c;
    int flags = PTR2INT(clientData);
    size_t length;
    Tcl_Obj *msgObj;
    Tk_State *statePtr = (Tk_State *) (widgRec + offset);

    if (value == NULL || *value == 0) {
	*statePtr = TK_STATE_NULL;
	return TCL_OK;
    }

    c = value[0];
    length = strlen(value);

    if ((c == 'n') && (strncmp(value, "normal", length) == 0)) {
	*statePtr = TK_STATE_NORMAL;
	return TCL_OK;
    }
    if ((c == 'd') && (strncmp(value, "disabled", length) == 0)) {
	*statePtr = TK_STATE_DISABLED;
	return TCL_OK;
    }
    if ((c == 'a') && (flags&1) && (strncmp(value, "active", length) == 0)) {
	*statePtr = TK_STATE_ACTIVE;
	return TCL_OK;
    }
    if ((c == 'h') && (flags&2) && (strncmp(value, "hidden", length) == 0)) {
	*statePtr = TK_STATE_HIDDEN;
	return TCL_OK;
    }

    msgObj = Tcl_ObjPrintf("bad %s value \"%s\": must be normal",
	    ((flags & 4) ? "-default" : "state"), value);
    if (flags & 1) {
	Tcl_AppendToObj(msgObj, ", active", TCL_INDEX_NONE);
    }
    if (flags & 2) {
	Tcl_AppendToObj(msgObj, ", hidden", TCL_INDEX_NONE);
    }
    if (flags & 3) {
	Tcl_AppendToObj(msgObj, ",", TCL_INDEX_NONE);
    }
    Tcl_AppendToObj(msgObj, " or disabled", TCL_INDEX_NONE);
    Tcl_SetObjResult(interp, msgObj);
    Tcl_SetErrorCode(interp, "TK", "VALUE", "STATE", (char *)NULL);
    *statePtr = TK_STATE_NORMAL;
    return TCL_ERROR;
}

/*
 *--------------------------------------------------------------
 *
 * TkStatePrintProc --
 *
 *	This function is invoked by the Tk configuration code to produce a
 *	printable string for the "-state" configuration option.
 *
 * Results:
 *	The return value is a string describing the state for the item
 *	referred to by "widgRec". In addition, *freeProcPtr is filled in with
 *	the address of a function to call to free the result string when it's
 *	no longer needed (or NULL to indicate that the string doesn't need to
 *	be freed).
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */

const char *
TkStatePrintProc(
    TCL_UNUSED(void *),	/* Ignored. */
    TCL_UNUSED(Tk_Window),		/* Window containing canvas widget. */
    char *widgRec,		/* Pointer to record for item. */
    Tcl_Size offset,			/* Offset into item. */
    TCL_UNUSED(Tcl_FreeProc **))	/* Pointer to variable to fill in with
				 * information about how to reclaim storage
				 * for return string. */
{
    Tk_State *statePtr = (Tk_State *) (widgRec + offset);

    switch (*statePtr) {
    case TK_STATE_NORMAL:
	return "normal";
    case TK_STATE_DISABLED:
	return "disabled";
    case TK_STATE_HIDDEN:
	return "hidden";
    case TK_STATE_ACTIVE:
	return "active";
    default:
	return "";
    }
}

/*
 *--------------------------------------------------------------
 *
 * TkOrientParseProc --
 *
 *	This function is invoked during option processing to handle the
 *	"-orient" option.
 *
 * Results:
 *	A standard Tcl return value.
 *
 * Side effects:
 *	The orientation for a given item gets replaced by the orientation
 *	indicated in the value argument.
 *
 *--------------------------------------------------------------
 */

int
TkOrientParseProc(
    TCL_UNUSED(void *),	/* some flags.*/
    Tcl_Interp *interp,		/* Used for reporting errors. */
    TCL_UNUSED(Tk_Window),		/* Window containing canvas widget. */
    const char *value,		/* Value of option. */
    char *widgRec,		/* Pointer to record for item. */
    Tcl_Size offset)			/* Offset into item. */
{
    int c;
    size_t length;
    int *orientPtr = (int *) (widgRec + offset);

    if (value == NULL || *value == 0) {
	*orientPtr = 0;
	return TCL_OK;
    }

    c = value[0];
    length = strlen(value);

    if ((c == 'h') && (strncmp(value, "horizontal", length) == 0)) {
	*orientPtr = 0;
	return TCL_OK;
    }
    if ((c == 'v') && (strncmp(value, "vertical", length) == 0)) {
	*orientPtr = 1;
	return TCL_OK;
    }
    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
	    "bad orientation \"%s\": must be vertical or horizontal",
	    value));
    Tcl_SetErrorCode(interp, "TK", "VALUE", "ORIENTATION", (char *)NULL);
    *orientPtr = 0;
    return TCL_ERROR;
}

/*
 *--------------------------------------------------------------
 *
 * TkOrientPrintProc --
 *
 *	This function is invoked by the Tk configuration code to produce a
 *	printable string for the "-orient" configuration option.
 *
 * Results:
 *	The return value is a string describing the orientation for the item
 *	referred to by "widgRec". In addition, *freeProcPtr is filled in with
 *	the address of a function to call to free the result string when it's
 *	no longer needed (or NULL to indicate that the string doesn't need to
 *	be freed).
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------
 */

const char *
TkOrientPrintProc(
    TCL_UNUSED(void *),	/* Ignored. */
    TCL_UNUSED(Tk_Window),		/* Window containing canvas widget. */
    char *widgRec,		/* Pointer to record for item. */
    Tcl_Size offset,			/* Offset into item. */
    TCL_UNUSED(Tcl_FreeProc **))	/* Pointer to variable to fill in with
				 * information about how to reclaim storage
				 * for return string. */
{
    int *statePtr = (int *) (widgRec + offset);

    if (*statePtr) {
	return "vertical";
    } else {
	return "horizontal";
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkOffsetParseProc --
 *
 *	Converts the offset of a stipple or tile into the Tk_TSOffset
 *	structure.
 *
 *----------------------------------------------------------------------
 */

int
TkOffsetParseProc(
    void *clientData,	/* not used */
    Tcl_Interp *interp,		/* Interpreter to send results back to */
    Tk_Window tkwin,		/* Window on same display as tile */
    const char *value,		/* Name of image */
    char *widgRec,		/* Widget structure record */
    Tcl_Size offset)			/* Offset of tile in record */
{
    Tk_TSOffset *offsetPtr = (Tk_TSOffset *) (widgRec + offset);
    Tk_TSOffset tsoffset;
    const char *q, *p;
    int result;
    Tcl_Obj *msgObj;

    if ((value == NULL) || (*value == 0)) {
	tsoffset.flags = TK_OFFSET_CENTER|TK_OFFSET_MIDDLE;
	goto goodTSOffset;
    }
    tsoffset.flags = 0;
    p = value;

    switch (value[0]) {
    case '#':
	if (PTR2INT(clientData) & TK_OFFSET_RELATIVE) {
	    tsoffset.flags = TK_OFFSET_RELATIVE;
	    p++;
	    break;
	}
	goto badTSOffset;
    case 'e':
	switch(value[1]) {
	case '\0':
	    tsoffset.flags = TK_OFFSET_RIGHT|TK_OFFSET_MIDDLE;
	    goto goodTSOffset;
	case 'n':
	    if (value[2]!='d' || value[3]!='\0') {
		goto badTSOffset;
	    }
	    tsoffset.flags = INT_MAX;
	    goto goodTSOffset;
	}
	break;
    case 'w':
	if (value[1] != '\0') {goto badTSOffset;}
	tsoffset.flags = TK_OFFSET_LEFT|TK_OFFSET_MIDDLE;
	goto goodTSOffset;
    case 'n':
	if ((value[1] != '\0') && (value[2] != '\0')) {
	    goto badTSOffset;
	}
	switch(value[1]) {
	case '\0':
	    tsoffset.flags = TK_OFFSET_CENTER|TK_OFFSET_TOP;
	    goto goodTSOffset;
	case 'w':
	    tsoffset.flags = TK_OFFSET_LEFT|TK_OFFSET_TOP;
	    goto goodTSOffset;
	case 'e':
	    tsoffset.flags = TK_OFFSET_RIGHT|TK_OFFSET_TOP;
	    goto goodTSOffset;
	}
	goto badTSOffset;
    case 's':
	if ((value[1] != '\0') && (value[2] != '\0')) {
	    goto badTSOffset;
	}
	switch(value[1]) {
	case '\0':
	    tsoffset.flags = TK_OFFSET_CENTER|TK_OFFSET_BOTTOM;
	    goto goodTSOffset;
	case 'w':
	    tsoffset.flags = TK_OFFSET_LEFT|TK_OFFSET_BOTTOM;
	    goto goodTSOffset;
	case 'e':
	    tsoffset.flags = TK_OFFSET_RIGHT|TK_OFFSET_BOTTOM;
	    goto goodTSOffset;
	}
	goto badTSOffset;
    case 'c':
	if (strncmp(value, "center", strlen(value)) != 0) {
	    goto badTSOffset;
	}
	tsoffset.flags = TK_OFFSET_CENTER|TK_OFFSET_MIDDLE;
	goto goodTSOffset;
    }

    /*
     * Check for an extra offset.
     */

    q = strchr(p, ',');
    if (q == NULL) {
	if (PTR2INT(clientData) & TK_OFFSET_INDEX) {
	    if (Tcl_GetInt(interp, (char *) p, &tsoffset.flags) != TCL_OK) {
		Tcl_ResetResult(interp);
		goto badTSOffset;
	    }
	    tsoffset.flags |= TK_OFFSET_INDEX;
	    goto goodTSOffset;
	}
	goto badTSOffset;
    }

    *((char *) q) = 0;
    result = Tk_GetPixels(interp, tkwin, (char *) p, &tsoffset.xoffset);
    *((char *) q) = ',';
    if (result != TCL_OK) {
	return TCL_ERROR;
    }
    if (Tk_GetPixels(interp, tkwin, (char*)q+1, &tsoffset.yoffset) != TCL_OK) {
	return TCL_ERROR;
    }

    /*
     * Below is a hack to allow the stipple/tile offset to be stored in the
     * internal tile structure. Most of the times, offsetPtr is a pointer to
     * an already existing tile structure. However if this structure is not
     * already created, we must do it with Tk_GetTile()!!!!
     */

  goodTSOffset:
    memcpy(offsetPtr, &tsoffset, sizeof(Tk_TSOffset));
    return TCL_OK;

  badTSOffset:
    msgObj = Tcl_ObjPrintf("bad offset \"%s\": expected \"x,y\"", value);
    if (PTR2INT(clientData) & TK_OFFSET_RELATIVE) {
	Tcl_AppendToObj(msgObj, ", \"#x,y\"", TCL_INDEX_NONE);
    }
    if (PTR2INT(clientData) & TK_OFFSET_INDEX) {
	Tcl_AppendToObj(msgObj, ", <index>", TCL_INDEX_NONE);
    }
    Tcl_AppendToObj(msgObj, ", n, ne, e, se, s, sw, w, nw, or center", TCL_INDEX_NONE);
    Tcl_SetObjResult(interp, msgObj);
    Tcl_SetErrorCode(interp, "TK", "VALUE", "OFFSET", (char *)NULL);
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * TkOffsetPrintProc --
 *
 *	Returns the offset of the tile.
 *
 * Results:
 *	The offset of the tile is returned.
 *
 *----------------------------------------------------------------------
 */

const char *
TkOffsetPrintProc(
    TCL_UNUSED(void *),	/* not used */
    TCL_UNUSED(Tk_Window),		/* not used */
    char *widgRec,		/* Widget structure record */
    Tcl_Size offset,			/* Offset of tile in record */
    Tcl_FreeProc **freeProcPtr)	/* not used */
{
    Tk_TSOffset *offsetPtr = (Tk_TSOffset *) (widgRec + offset);
    char *p, *q;

    if (offsetPtr->flags & TK_OFFSET_INDEX) {
	if (offsetPtr->flags >= INT_MAX) {
	    return "end";
	}
	p = (char *)ckalloc(32);
	snprintf(p, 32, "%d", offsetPtr->flags & ~TK_OFFSET_INDEX);
	*freeProcPtr = TCL_DYNAMIC;
	return p;
    }
    if (offsetPtr->flags & TK_OFFSET_TOP) {
	if (offsetPtr->flags & TK_OFFSET_LEFT) {
	    return "nw";
	} else if (offsetPtr->flags & TK_OFFSET_CENTER) {
	    return "n";
	} else if (offsetPtr->flags & TK_OFFSET_RIGHT) {
	    return "ne";
	}
    } else if (offsetPtr->flags & TK_OFFSET_MIDDLE) {
	if (offsetPtr->flags & TK_OFFSET_LEFT) {
	    return "w";
	} else if (offsetPtr->flags & TK_OFFSET_CENTER) {
	    return "center";
	} else if (offsetPtr->flags & TK_OFFSET_RIGHT) {
	    return "e";
	}
    } else if (offsetPtr->flags & TK_OFFSET_BOTTOM) {
	if (offsetPtr->flags & TK_OFFSET_LEFT) {
	    return "sw";
	} else if (offsetPtr->flags & TK_OFFSET_CENTER) {
	    return "s";
	} else if (offsetPtr->flags & TK_OFFSET_RIGHT) {
	    return "se";
	}
    }
    q = p = (char *)ckalloc(32);
    if (offsetPtr->flags & TK_OFFSET_RELATIVE) {
	*q++ = '#';
    }
    snprintf(q, 32, "%d,%d", offsetPtr->xoffset, offsetPtr->yoffset);
    *freeProcPtr = TCL_DYNAMIC;
    return p;
}

/*
 *----------------------------------------------------------------------
 *
 * TkPixelParseProc --
 *
 *	Converts the name of an image into a tile.
 *
 *----------------------------------------------------------------------
 */

int
TkPixelParseProc(
    void *clientData,	/* If non-NULL, negative values are allowed as
				 * well. */
    Tcl_Interp *interp,		/* Interpreter to send results back to */
    Tk_Window tkwin,		/* Window on same display as tile */
    const char *value,		/* Name of image */
    char *widgRec,		/* Widget structure record */
    Tcl_Size offset)			/* Offset of tile in record */
{
    double *doublePtr = (double *) (widgRec + offset);
    int result;

    result = TkGetDoublePixels(interp, tkwin, value, doublePtr);

    if ((result == TCL_OK) && (clientData == NULL) && (*doublePtr < 0.0)) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"expected screen distance but got \"%.50s\"", value));
	Tcl_SetErrorCode(interp, "TK", "VALUE", "PIXELS", (char *)NULL);
	return TCL_ERROR;
    }
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * TkPixelPrintProc --
 *
 *	Returns the name of the tile.
 *
 * Results:
 *	The name of the tile is returned.
 *
 *----------------------------------------------------------------------
 */

const char *
TkPixelPrintProc(
    TCL_UNUSED(void *),	/* not used */
    TCL_UNUSED(Tk_Window),		/* not used */
    char *widgRec,		/* Widget structure record */
    Tcl_Size offset,			/* Offset of tile in record */
    Tcl_FreeProc **freeProcPtr)	/* not used */
{
    double *doublePtr = (double *) (widgRec + offset);
    char *p = (char *)ckalloc(24);

    Tcl_PrintDouble(NULL, *doublePtr, p);
    *freeProcPtr = TCL_DYNAMIC;
    return p;
}

/*
 *----------------------------------------------------------------------
 *
 * TkDrawInsetFocusHighlight --
 *
 *	This function draws a rectangular ring around the outside of a widget
 *	to indicate that it has received the input focus. It takes an
 *	additional padding argument that specifies how much padding is present
 *	outside the widget.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A rectangle "width" pixels wide is drawn in "drawable", corresponding
 *	to the outer area of "tkwin".
 *
 *----------------------------------------------------------------------
 */

void
TkDrawInsetFocusHighlight(
    Tk_Window tkwin,		/* Window whose focus highlight ring is to be
				 * drawn. */
    GC gc,			/* Graphics context to use for drawing the
				 * highlight ring. */
    int width,			/* Width of the highlight ring, in pixels. */
    Drawable drawable,		/* Where to draw the ring (typically a pixmap
				 * for double buffering). */
    int padding)		/* Width of padding outside of widget. */
{
    XRectangle rects[4];

    rects[0].x = padding;
    rects[0].y = padding;
    rects[0].width = Tk_Width(tkwin) - (2 * padding);
    rects[0].height = width;
    rects[1].x = padding;
    rects[1].y = Tk_Height(tkwin) - width - padding;
    rects[1].width = Tk_Width(tkwin) - (2 * padding);
    rects[1].height = width;
    rects[2].x = padding;
    rects[2].y = width + padding;
    rects[2].width = width;
    rects[2].height = Tk_Height(tkwin) - 2*width - 2*padding;
    rects[3].x = Tk_Width(tkwin) - width - padding;
    rects[3].y = rects[2].y;
    rects[3].width = width;
    rects[3].height = rects[2].height;
    XFillRectangles(Tk_Display(tkwin), drawable, gc, rects, 4);
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_DrawFocusHighlight --
 *
 *	This function draws a rectangular ring around the outside of a widget
 *	to indicate that it has received the input focus.
 *
 *	This function is now deprecated. Use Tk_DrawHighlightBorder instead,
 *	since this function does not handle drawing the Focus ring properly on
 *	the Macintosh - you need to know the background GC as well as the
 *	foreground since the Mac focus ring separated from the widget by a 1
 *	pixel border.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A rectangle "width" pixels wide is drawn in "drawable", corresponding
 *	to the outer area of "tkwin".
 *
 *----------------------------------------------------------------------
 */

void
Tk_DrawFocusHighlight(
    Tk_Window tkwin,		/* Window whose focus highlight ring is to be
				 * drawn. */
    GC gc,			/* Graphics context to use for drawing the
				 * highlight ring. */
    int width,			/* Width of the highlight ring, in pixels. */
    Drawable drawable)		/* Where to draw the ring (typically a pixmap
				 * for double buffering). */
{
    TkDrawInsetFocusHighlight(tkwin, gc, width, drawable, 0);
}

/*
 *----------------------------------------------------------------------
 *
 * TkDrawDottedRect --
 *
 *	This function draws a dotted rectangle, used as focus ring of Ttk
 *	widgets and for rendering the active element of a listbox.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A dotted rectangle is drawn in the specified Drawable.  On the
 *	windowing systems x11 and aqua the GC components line_style,
 *	line_width, dashes, and dash_offset are modified as needed.
 *
 *----------------------------------------------------------------------
 */

void
TkDrawDottedRect(
    Display *disp,		/* Display containing the dotted rectangle. */
    Drawable d,			/* Where to draw the rectangle (typically a
				 * pixmap for double buffering). */
    GC gc,			/* Graphics context to use for drawing the
				 * rectangle. */
    int x, int y,		/* Coordinates of the top-left corner. */
    int width, int height)	/* Width & height, _including the border_. */
{
#ifdef _WIN32
    TkWinDrawDottedRect(disp, d, gc->foreground, x, y, width, height);

#else
    XGCValues gcValues;
    int widthMod2 = width % 2, heightMod2 = height % 2;
    int x2 = x + width - 1, y2 = y + height - 1;

    gcValues.line_style = LineOnOffDash;
    gcValues.line_width = 1;
    gcValues.dashes = 1;
#ifdef MAC_OSX_TK
    gcValues.dash_offset = 1;
#else
    gcValues.dash_offset = 0;
#endif
    XChangeGC(disp, gc, GCLineStyle | GCLineWidth | GCDashList | GCDashOffset,
	    &gcValues);

    if (widthMod2 == 0 && heightMod2 == 0) {
	XDrawLine(disp, d, gc, x+1, y,  x2-1, y);	/* N */
	XDrawLine(disp, d, gc, x+2, y2, x2,   y2);	/* S */
	XDrawLine(disp, d, gc, x,  y+2, x,  y2);	/* W */
	XDrawLine(disp, d, gc, x2, y+1, x2, y2-1);	/* E */
    } else {
	int dx = 1 - widthMod2, dy = 1 - heightMod2;

	XDrawLine(disp, d, gc, x+1, y,  x2-dx, y);	/* N */
	XDrawLine(disp, d, gc, x+1, y2, x2-dx, y2);	/* S */
	XDrawLine(disp, d, gc, x,  y+1, x,  y2-dy);	/* W */
	XDrawLine(disp, d, gc, x2, y+1, x2, y2-dy);	/* E */
    }
#endif
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_GetScrollInfo --
 *
 *	This function is invoked to parse "xview" and "yview" scrolling
 *	commands for widgets using the new scrolling command syntax ("moveto"
 *	or "scroll" options).
 *
 * Results:
 *	The return value is either TK_SCROLL_MOVETO, TK_SCROLL_PAGES,
 *	TK_SCROLL_UNITS, or TK_SCROLL_ERROR. This indicates whether the
 *	command was successfully parsed and what form the command took. If
 *	TK_SCROLL_MOVETO, *dblPtr is filled in with the desired position; if
 *	TK_SCROLL_PAGES or TK_SCROLL_UNITS, *intPtr is filled in with the
 *	number of lines to move (may be negative); if TK_SCROLL_ERROR, the
 *	interp's result contains an error message.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Tk_GetScrollInfo(
    Tcl_Interp *interp,		/* Used for error reporting. */
    Tcl_Size argc,			/* # arguments for command. */
    const char **argv,		/* Arguments for command. */
    double *dblPtr,		/* Filled in with argument "moveto" option, if
				 * any. */
    int *intPtr)		/* Filled in with number of pages or lines to
				 * scroll, if any. */
{
    int c = argv[2][0];
    size_t length = strlen(argv[2]);

    if ((c == 'm') && (strncmp(argv[2], "moveto", length) == 0)) {
	if (argc != 4) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "wrong # args: should be \"%s %s %s\"",
		    argv[0], argv[1], "moveto fraction"));
	    Tcl_SetErrorCode(interp, "TCL", "WRONGARGS", (char *)NULL);
	    return TK_SCROLL_ERROR;
	}
	if (Tcl_GetDouble(interp, argv[3], dblPtr) != TCL_OK) {
	    return TK_SCROLL_ERROR;
	}
	return TK_SCROLL_MOVETO;
    } else if ((c == 's')
	    && (strncmp(argv[2], "scroll", length) == 0)) {
	double d;
	if (argc != 5) {
	    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		    "wrong # args: should be \"%s %s %s\"",
		    argv[0], argv[1], "scroll number pages|units"));
	    Tcl_SetErrorCode(interp, "TCL", "WRONGARGS", (char *)NULL);
	    return TK_SCROLL_ERROR;
	}
	if (Tcl_GetDouble(interp, argv[3], &d) != TCL_OK) {
	    return TK_SCROLL_ERROR;
	}
	*intPtr = (d > 0) ? ceil(d) : floor(d);
	length = strlen(argv[4]);
	c = argv[4][0];
	if ((c == 'p') && (strncmp(argv[4], "pages", length) == 0)) {
	    return TK_SCROLL_PAGES;
	} else if ((c == 'u') && (strncmp(argv[4], "units", length) == 0)) {
	    return TK_SCROLL_UNITS;
	}

	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"bad argument \"%s\": must be pages or units", argv[4]));
	Tcl_SetErrorCode(interp, "TK", "VALUE", "SCROLL_UNITS", (char *)NULL);
	return TK_SCROLL_ERROR;
    }
    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
	    "unknown option \"%s\": must be moveto or scroll", argv[2]));
    Tcl_SetErrorCode(interp, "TCL", "LOOKUP", "INDEX", "option", argv[2],
	    (char *)NULL);
    return TK_SCROLL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_GetScrollInfoObj --
 *
 *	This function is invoked to parse "xview" and "yview" scrolling
 *	commands for widgets using the new scrolling command syntax ("moveto"
 *	or "scroll" options).
 *
 * Results:
 *	The return value is either TK_SCROLL_MOVETO, TK_SCROLL_PAGES,
 *	TK_SCROLL_UNITS, or TK_SCROLL_ERROR. This indicates whether the
 *	command was successfully parsed and what form the command took. If
 *	TK_SCROLL_MOVETO, *dblPtr is filled in with the desired position; if
 *	TK_SCROLL_PAGES or TK_SCROLL_UNITS, *intPtr is filled in with the
 *	number of lines to move (may be negative); if TK_SCROLL_ERROR, the
 *	interp's result contains an error message.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Tk_GetScrollInfoObj(
    Tcl_Interp *interp,		/* Used for error reporting. */
    Tcl_Size objc,			/* # arguments for command. */
    Tcl_Obj *const objv[],	/* Arguments for command. */
    double *dblPtr,		/* Filled in with argument "moveto" option, if
				 * any. */
    int *intPtr)		/* Filled in with number of pages or lines to
				 * scroll, if any. */
{
    Tcl_Size length;
    const char *arg;

    if (objc + 1 < 5) {
	Tcl_WrongNumArgs(interp, 2, objv, "moveto|scroll args");
	return TK_SCROLL_ERROR;
    }
    arg = Tcl_GetStringFromObj(objv[2], &length);
#define ArgPfxEq(str) \
	((arg[0] == str[0]) && !strncmp(arg, str, length))

    if (ArgPfxEq("moveto")) {
	if (objc != 4) {
	    Tcl_WrongNumArgs(interp, 2, objv, "moveto fraction");
	    return TK_SCROLL_ERROR;
	}
	if (Tcl_GetDoubleFromObj(interp, objv[3], dblPtr) != TCL_OK) {
	    return TK_SCROLL_ERROR;
	}
	return TK_SCROLL_MOVETO;
    } else if (ArgPfxEq("scroll")) {
	double d;
	if (objc != 5) {
	    Tcl_WrongNumArgs(interp, 2, objv, "scroll number pages|units");
	    return TK_SCROLL_ERROR;
	}
	if (Tcl_GetDoubleFromObj(interp, objv[3], &d) != TCL_OK) {
	    return TK_SCROLL_ERROR;
	}
	*intPtr = (d >= 0) ? ceil(d) : floor(d);
	if (dblPtr) {
	    *dblPtr = d;
	}

	arg = Tcl_GetStringFromObj(objv[4], &length);
	if (ArgPfxEq("pages")) {
	    return TK_SCROLL_PAGES;
	} else if (ArgPfxEq("units")) {
	    return TK_SCROLL_UNITS;
	}

	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"bad argument \"%s\": must be pages or units", arg));
	Tcl_SetErrorCode(interp, "TK", "VALUE", "SCROLL_UNITS", (char *)NULL);
	return TK_SCROLL_ERROR;
    }
    Tcl_SetObjResult(interp, Tcl_ObjPrintf(
	    "unknown option \"%s\": must be moveto or scroll", arg));
    Tcl_SetErrorCode(interp, "TCL", "LOOKUP", "INDEX", "option", arg, (char *)NULL);
    return TK_SCROLL_ERROR;
}

/*
 *---------------------------------------------------------------------------
 *
 * TkComputeAnchor --
 *
 *	Determine where to place a rectangle so that it will be properly
 *	anchored with respect to the given window. Used by widgets to align a
 *	box of text inside a window. When anchoring with respect to one of the
 *	sides, the rectangle be placed inside of the internal border of the
 *	window.
 *
 * Results:
 *	*xPtr and *yPtr set to the upper-left corner of the rectangle anchored
 *	in the window.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

void
TkComputeAnchor(
    Tk_Anchor anchor,		/* Desired anchor. */
    Tk_Window tkwin,		/* Anchored with respect to this window. */
    int padX, int padY,		/* Use this extra padding inside window, in
				 * addition to the internal border. */
    int innerWidth, int innerHeight,
				/* Size of rectangle to anchor in window. */
    int *xPtr, int *yPtr)	/* Returns upper-left corner of anchored
				 * rectangle. */
{
    /*
     * Handle the horizontal parts.
     */

    switch (anchor) {
    case TK_ANCHOR_NW:
    case TK_ANCHOR_W:
    case TK_ANCHOR_SW:
	*xPtr = Tk_InternalBorderLeft(tkwin) + padX;
	break;

    case TK_ANCHOR_NE:
    case TK_ANCHOR_E:
    case TK_ANCHOR_SE:
	*xPtr = Tk_Width(tkwin) - Tk_InternalBorderRight(tkwin) - padX
		- innerWidth;
	break;

    default:
	*xPtr = (Tk_Width(tkwin) - innerWidth - Tk_InternalBorderLeft(tkwin) -
		Tk_InternalBorderRight(tkwin)) / 2 +
		Tk_InternalBorderLeft(tkwin);
	break;
    }

    /*
     * Handle the vertical parts.
     */

    switch (anchor) {
    case TK_ANCHOR_NW:
    case TK_ANCHOR_N:
    case TK_ANCHOR_NE:
	*yPtr = Tk_InternalBorderTop(tkwin) + padY;
	break;

    case TK_ANCHOR_SW:
    case TK_ANCHOR_S:
    case TK_ANCHOR_SE:
	*yPtr = Tk_Height(tkwin) - Tk_InternalBorderBottom(tkwin) - padY
		- innerHeight;
	break;

    default:
	*yPtr = (Tk_Height(tkwin) - innerHeight- Tk_InternalBorderTop(tkwin) -
		Tk_InternalBorderBottom(tkwin)) / 2 +
		Tk_InternalBorderTop(tkwin);
	break;
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * TkFindStateString --
 *
 *	Given a lookup table, map a number to a string in the table.
 *
 * Results:
 *	If numKey was equal to the numeric key of one of the elements in the
 *	table, returns the string key of that element. Returns NULL if numKey
 *	was not equal to any of the numeric keys in the table.
 *
 * Side effects.
 *	None.
 *
 *---------------------------------------------------------------------------
 */

const char *
TkFindStateString(
    const TkStateMap *mapPtr,	/* The state table. */
    int numKey)			/* The key to try to find in the table. */
{
    for (; mapPtr->strKey!=NULL ; mapPtr++) {
	if (numKey == mapPtr->numKey) {
	    return mapPtr->strKey;
	}
    }
    return NULL;
}

/*
 *---------------------------------------------------------------------------
 *
 * TkFindStateNum, TkFindStateNumObj --
 *
 *	Given a lookup table, map a string to a number in the table.
 *
 * Results:
 *	If strKey was equal to the string keys of one of the elements in the
 *	table, returns the numeric key of that element. Returns the numKey
 *	associated with the last element (the NULL string one) in the table if
 *	strKey was not equal to any of the string keys in the table. In that
 *	case, an error message is also left in the interp's result (if interp
 *	is not NULL).
 *
 * Side effects.
 *	None.
 *
 *---------------------------------------------------------------------------
 */

int
TkFindStateNum(
    Tcl_Interp *interp,		/* Interp for error reporting. */
    const char *option,		/* String to use when constructing error. */
    const TkStateMap *mapPtr,	/* Lookup table. */
    const char *strKey)		/* String to try to find in lookup table. */
{
    const TkStateMap *mPtr;

    /*
     * See if the value is in the state map.
     */

    for (mPtr = mapPtr; mPtr->strKey != NULL; mPtr++) {
	if (strcmp(strKey, mPtr->strKey) == 0) {
	    return mPtr->numKey;
	}
    }

    /*
     * Not there. Generate an error message (if we can) and return the
     * default.
     */

    if (interp != NULL) {
	Tcl_Obj *msgObj;

	mPtr = mapPtr;
	msgObj = Tcl_ObjPrintf("bad %s value \"%s\": must be %s",
		option, strKey, mPtr->strKey);
	for (mPtr++; mPtr->strKey != NULL; mPtr++) {
	    Tcl_AppendPrintfToObj(msgObj, ",%s %s",
		    ((mPtr[1].strKey != NULL) ? "" : "or "), mPtr->strKey);
	}
	Tcl_SetObjResult(interp, msgObj);
	Tcl_SetErrorCode(interp, "TK", "LOOKUP", option, strKey, (char *)NULL);
    }
    return mPtr->numKey;
}

int
TkFindStateNumObj(
    Tcl_Interp *interp,		/* Interp for error reporting. */
    Tcl_Obj *optionPtr,		/* String to use when constructing error. */
    const TkStateMap *mapPtr,	/* Lookup table. */
    Tcl_Obj *keyPtr)		/* String key to find in lookup table. */
{
    const TkStateMap *mPtr;
    const char *key;
    const Tcl_ObjType *typePtr;

    /*
     * See if the value is in the object cache.
     */

    if ((keyPtr->typePtr == &tkStateKeyObjType.objType)
	    && (keyPtr->internalRep.twoPtrValue.ptr1 == mapPtr)) {
	return PTR2INT(keyPtr->internalRep.twoPtrValue.ptr2);
    }

    /*
     * Not there. Look in the state map.
     */

    key = Tcl_GetString(keyPtr);
    for (mPtr = mapPtr; mPtr->strKey != NULL; mPtr++) {
	if (strcmp(key, mPtr->strKey) == 0) {
	    typePtr = keyPtr->typePtr;
	    if ((typePtr != NULL) && (typePtr->freeIntRepProc != NULL)) {
		typePtr->freeIntRepProc(keyPtr);
	    }
	    keyPtr->internalRep.twoPtrValue.ptr1 = (void *) mapPtr;
	    keyPtr->internalRep.twoPtrValue.ptr2 = INT2PTR(mPtr->numKey);
	    keyPtr->typePtr = &tkStateKeyObjType.objType;
	    return mPtr->numKey;
	}
    }

    /*
     * Not there either. Generate an error message (if we can) and return the
     * default.
     */

    if (interp != NULL) {
	Tcl_Obj *msgObj;

	mPtr = mapPtr;
	msgObj = Tcl_ObjPrintf(
		"bad %s value \"%s\": must be %s",
		Tcl_GetString(optionPtr), key, mPtr->strKey);
	for (mPtr++; mPtr->strKey != NULL; mPtr++) {
	    Tcl_AppendPrintfToObj(msgObj, ",%s %s",
		    ((mPtr[1].strKey != NULL) ? "" : " or"), mPtr->strKey);
	}
	Tcl_SetObjResult(interp, msgObj);
	Tcl_SetErrorCode(interp, "TK", "LOOKUP", Tcl_GetString(optionPtr),
		key, (char *)NULL);
    }
    return mPtr->numKey;
}

/*
 * ----------------------------------------------------------------------
 *
 * TkBackgroundEvalObjv --
 *
 *	Evaluate a command while ensuring that we do not affect the
 *	interpreters state. This is important when evaluating script
 *	during background tasks.
 *
 * Results:
 *	A standard Tcl result code.
 *
 * Side Effects:
 *	The interpreters variables and code may be modified by the script
 *	but the result will not be modified.
 *
 * ----------------------------------------------------------------------
 */

int
TkBackgroundEvalObjv(
    Tcl_Interp *interp,
    Tcl_Size objc,
    Tcl_Obj *const *objv,
    int flags)
{
    Tcl_InterpState state;
    int r = TCL_OK;
    Tcl_Size n;

    /*
     * Record the state of the interpreter.
     */

    Tcl_Preserve(interp);
    state = Tcl_SaveInterpState(interp, TCL_OK);

    /*
     * Evaluate the command and handle any error.
     */

    for (n = 0; n < objc; ++n) {
	Tcl_IncrRefCount(objv[n]);
    }
    r = Tcl_EvalObjv(interp, objc, objv, flags);
    for (n = 0; n < objc; ++n) {
	Tcl_DecrRefCount(objv[n]);
    }
    if (r == TCL_ERROR) {
	Tcl_AddErrorInfo(interp, "\n    (background event handler)");
	Tcl_BackgroundException(interp, r);
    }

    /*
     * Restore the state of the interpreter.
     */

    (void) Tcl_RestoreInterpState(interp, state);
    Tcl_Release(interp);

    return r;
}

/*
 *----------------------------------------------------------------------
 *
 * TkMakeEnsemble --
 *
 *	Create an ensemble from a table of implementation commands. This may
 *	be called recursively to create sub-ensembles.
 *
 * Results:
 *	Handle for the ensemble, or NULL if creation of it fails.
 *
 *----------------------------------------------------------------------
 */

Tcl_Command
TkMakeEnsemble(
    Tcl_Interp *interp,
    const char *namesp,
    const char *name,
    void *clientData,
    const TkEnsemble map[])
{
    Tcl_Namespace *namespacePtr = NULL;
    Tcl_Command ensemble = NULL;
    Tcl_Obj *dictObj = NULL, *nameObj;
    Tcl_DString ds;
    int i;

    if (map == NULL) {
	return NULL;
    }

    Tcl_DStringInit(&ds);

    namespacePtr = Tcl_FindNamespace(interp, namesp, NULL, 0);
    if (namespacePtr == NULL) {
	namespacePtr = Tcl_CreateNamespace(interp, namesp, NULL, NULL);
	if (namespacePtr == NULL) {
	    Tcl_Panic("failed to create namespace \"%s\"", namesp);
	}
    }

    nameObj = Tcl_NewStringObj(name, TCL_INDEX_NONE);
    ensemble = Tcl_FindEnsemble(interp, nameObj, 0);
    Tcl_DecrRefCount(nameObj);
    if (ensemble == NULL) {
	ensemble = Tcl_CreateEnsemble(interp, name, namespacePtr,
		TCL_ENSEMBLE_PREFIX);
	if (ensemble == NULL) {
	    Tcl_Panic("failed to create ensemble \"%s\"", name);
	}
    }

    Tcl_DStringSetLength(&ds, 0);
    Tcl_DStringAppend(&ds, namesp, TCL_INDEX_NONE);
    if (!(strlen(namesp) == 2 && namesp[1] == ':')) {
	Tcl_DStringAppend(&ds, "::", TCL_INDEX_NONE);
    }
    Tcl_DStringAppend(&ds, name, TCL_INDEX_NONE);

    dictObj = Tcl_NewObj();
    for (i = 0; map[i].name != NULL ; ++i) {
	Tcl_Obj *fqdnObj;

	nameObj = Tcl_NewStringObj(map[i].name, TCL_INDEX_NONE);
	fqdnObj = Tcl_NewStringObj(Tcl_DStringValue(&ds),
		Tcl_DStringLength(&ds));
	Tcl_AppendStringsToObj(fqdnObj, "::", map[i].name, (char *)NULL);
	Tcl_DictObjPut(NULL, dictObj, nameObj, fqdnObj);
	if (map[i].proc) {
	    Tcl_CreateObjCommand2(interp, Tcl_GetString(fqdnObj),
		    map[i].proc, clientData, NULL);
	} else if (map[i].subensemble) {
	    TkMakeEnsemble(interp, Tcl_DStringValue(&ds),
		    map[i].name, clientData, map[i].subensemble);
	}
    }

    if (ensemble) {
	Tcl_SetEnsembleMappingDict(interp, ensemble, dictObj);
    }

    Tcl_DStringFree(&ds);
    return ensemble;
}

/*
 *----------------------------------------------------------------------
 *
 * TkScalingLevel --
 *
 *	Returns the display's DPI scaling level as 1.0, 1.25, 1.5, ....
 *
 * Results:
 *      The scaling level.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

double
TkScalingLevel(
    Tk_Window tkwin)
{
    Tcl_Interp *interp = Tk_Interp(tkwin);
    Tcl_Obj *scalingPctPtr = Tcl_GetVar2Ex(interp, "::tk::scalingPct", NULL,
	    TCL_GLOBAL_ONLY);
    if (scalingPctPtr == NULL) {
	return 1.0;
    } else {
	int scalingPct;
	Tcl_GetIntFromObj(interp, scalingPctPtr, &scalingPct);
	return scalingPct / 100.0;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_SendVirtualEvent --
 *
 *	Send a virtual event notification to the specified target window.
 *	Equivalent to:
 *	    "event generate $target <<$eventName>> -data $detail"
 *
 *	Note that we use Tk_QueueWindowEvent, not Tk_HandleEvent, so this
 *	routine does not reenter the interpreter.
 *
 *----------------------------------------------------------------------
 */

void
Tk_SendVirtualEvent(
    Tk_Window target,
    const char *eventName,
    Tcl_Obj *detail)
{
    union {XEvent general; XVirtualEvent virt;} event;

    memset(&event, 0, sizeof(event));
    event.general.xany.type = VirtualEvent;
    event.general.xany.serial = NextRequest(Tk_Display(target));
    event.general.xany.send_event = False;
    event.general.xany.window = Tk_WindowId(target);
    event.general.xany.display = Tk_Display(target);
    event.virt.name = Tk_GetUid(eventName);
    event.virt.user_data = detail;
    if (detail) Tcl_IncrRefCount(detail); // Event code will DecrRefCount

    Tk_QueueWindowEvent(&event.general, TCL_QUEUE_TAIL);
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
