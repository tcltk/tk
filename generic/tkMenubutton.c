/*
 * tkMenubutton.c --
 *
 *	This module implements button-like widgets that are used to invoke
 *	pull-down menus.
 *
 * Copyright © 1990-1994 The Regents of the University of California.
 * Copyright © 1994-1997 Sun Microsystems, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkMenubutton.h"

/*
 * The structure below defines menubutton class behavior by means of
 * procedures that can be invoked from generic window code.
 */

static const Tk_ClassProcs menubuttonClass = {
    sizeof(Tk_ClassProcs),	/* size */
    TkMenuButtonWorldChanged,	/* worldChangedProc */
    NULL,			/* createProc */
    NULL			/* modalProc */
};

/*
 * The following table defines the legal values for the -direction option. It
 * is used together with the "enum direction" declaration in tkMenubutton.h.
 */

static const char *const directionStrings[] = {
    "above", "below", "flush", "left", "right", NULL
};

/*
 * Information used for parsing configuration specs:
 */

static const Tk_OptionSpec optionSpecs[] = {
    {TK_OPTION_BORDER, "-activebackground", "activeBackground", "Foreground",
	DEF_MENUBUTTON_ACTIVE_BG_COLOR, TCL_INDEX_NONE,
	offsetof(TkMenuButton, activeBorder), 0,
	DEF_MENUBUTTON_ACTIVE_BG_MONO, 0},
    {TK_OPTION_COLOR, "-activeforeground", "activeForeground", "Background",
	DEF_MENUBUTTON_ACTIVE_FG_COLOR, TCL_INDEX_NONE,
	offsetof(TkMenuButton, activeFg),
	0, DEF_MENUBUTTON_ACTIVE_FG_MONO, 0},
    {TK_OPTION_ANCHOR, "-anchor", "anchor", "Anchor",
	DEF_MENUBUTTON_ANCHOR, TCL_INDEX_NONE,
	offsetof(TkMenuButton, anchor), TK_OPTION_ENUM_VAR, 0, 0},
    {TK_OPTION_BORDER, "-background", "background", "Background",
	DEF_MENUBUTTON_BG_COLOR, TCL_INDEX_NONE, offsetof(TkMenuButton, normalBorder),
	0, DEF_MENUBUTTON_BG_MONO, 0},
    {TK_OPTION_SYNONYM, "-bd", NULL, NULL, NULL, 0, TCL_INDEX_NONE,
	0, "-borderwidth", 0},
    {TK_OPTION_SYNONYM, "-bg", NULL, NULL, NULL, 0, TCL_INDEX_NONE,
	0, "-background", 0},
    {TK_OPTION_BITMAP, "-bitmap", "bitmap", "Bitmap",
	DEF_MENUBUTTON_BITMAP, TCL_INDEX_NONE, offsetof(TkMenuButton, bitmap),
	TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_PIXELS, "-borderwidth", "borderWidth", "BorderWidth",
	DEF_MENUBUTTON_BORDER_WIDTH, offsetof(TkMenuButton, borderWidthObj),
	TCL_INDEX_NONE, 0, 0, 0},
    {TK_OPTION_CURSOR, "-cursor", "cursor", "Cursor",
	DEF_MENUBUTTON_CURSOR, TCL_INDEX_NONE, offsetof(TkMenuButton, cursor),
	TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_STRING_TABLE, "-direction", "direction", "Direction",
	DEF_MENUBUTTON_DIRECTION, TCL_INDEX_NONE, offsetof(TkMenuButton, direction),
	TK_OPTION_ENUM_VAR, directionStrings, 0},
    {TK_OPTION_COLOR, "-disabledforeground", "disabledForeground",
	"DisabledForeground", DEF_MENUBUTTON_DISABLED_FG_COLOR,
	TCL_INDEX_NONE, offsetof(TkMenuButton, disabledFg), TK_OPTION_NULL_OK,
	DEF_MENUBUTTON_DISABLED_FG_MONO, 0},
    {TK_OPTION_SYNONYM, "-fg", "foreground", NULL, NULL, 0, TCL_INDEX_NONE,
	0, "-foreground", 0},
    {TK_OPTION_FONT, "-font", "font", "Font",
	DEF_MENUBUTTON_FONT, TCL_INDEX_NONE, offsetof(TkMenuButton, tkfont), 0, 0, 0},
    {TK_OPTION_COLOR, "-foreground", "foreground", "Foreground",
	DEF_MENUBUTTON_FG, TCL_INDEX_NONE, offsetof(TkMenuButton, normalFg), 0, 0, 0},
    {TK_OPTION_STRING, "-height", "height", "Height",
	DEF_MENUBUTTON_HEIGHT, offsetof(TkMenuButton, heightObj),
	TCL_INDEX_NONE, 0, 0, 0},
    {TK_OPTION_COLOR, "-highlightbackground", "highlightBackground",
	"HighlightBackground", DEF_MENUBUTTON_HIGHLIGHT_BG_COLOR,
	TCL_INDEX_NONE, offsetof(TkMenuButton, highlightBgColorPtr), 0, 0, 0},
    {TK_OPTION_COLOR, "-highlightcolor", "highlightColor", "HighlightColor",
	DEF_MENUBUTTON_HIGHLIGHT, TCL_INDEX_NONE,
	offsetof(TkMenuButton, highlightColorPtr),	0, 0, 0},
    {TK_OPTION_PIXELS, "-highlightthickness", "highlightThickness",
	"HighlightThickness", DEF_MENUBUTTON_HIGHLIGHT_WIDTH,
	offsetof(TkMenuButton, highlightWidthObj), TCL_INDEX_NONE, 0, 0, 0},
    {TK_OPTION_STRING, "-image", "image", "Image",
	DEF_MENUBUTTON_IMAGE, offsetof(TkMenuButton, imageObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_BOOLEAN, "-indicatoron", "indicatorOn", "IndicatorOn",
	DEF_MENUBUTTON_INDICATOR, TCL_INDEX_NONE, offsetof(TkMenuButton, indicatorOn),
	0, 0, 0},
    {TK_OPTION_JUSTIFY, "-justify", "justify", "Justify",
	DEF_MENUBUTTON_JUSTIFY, TCL_INDEX_NONE, offsetof(TkMenuButton, justify), TK_OPTION_ENUM_VAR, 0, 0},
    {TK_OPTION_STRING, "-menu", "menu", "Menu",
	DEF_MENUBUTTON_MENU, offsetof(TkMenuButton, menuNameObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_PIXELS, "-padx", "padX", "Pad",
	DEF_MENUBUTTON_PADX, offsetof(TkMenuButton, padXObj), TCL_INDEX_NONE,
	0, 0, 0},
    {TK_OPTION_PIXELS, "-pady", "padY", "Pad",
	DEF_MENUBUTTON_PADY, offsetof(TkMenuButton, padYObj), TCL_INDEX_NONE,
	0, 0, 0},
    {TK_OPTION_RELIEF, "-relief", "relief", "Relief",
	DEF_MENUBUTTON_RELIEF, TCL_INDEX_NONE, offsetof(TkMenuButton, relief),
	0, 0, 0},
    {TK_OPTION_STRING_TABLE, "-compound", "compound", "Compound",
	DEF_BUTTON_COMPOUND, TCL_INDEX_NONE, offsetof(TkMenuButton, compound),
	0, tkCompoundStrings, 0},
    {TK_OPTION_STRING_TABLE, "-state", "state", "State",
	DEF_MENUBUTTON_STATE, TCL_INDEX_NONE, offsetof(TkMenuButton, state),
	TK_OPTION_ENUM_VAR, tkStateStrings, 0},
    {TK_OPTION_STRING, "-takefocus", "takeFocus", "TakeFocus",
	DEF_MENUBUTTON_TAKE_FOCUS, offsetof(TkMenuButton, takeFocusObj),
	TCL_INDEX_NONE, TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_STRING, "-text", "text", "Text",
	DEF_MENUBUTTON_TEXT, offsetof(TkMenuButton, textObj), TCL_INDEX_NONE, 0, 0, 0},
    {TK_OPTION_STRING, "-textvariable", "textVariable", "Variable",
	DEF_MENUBUTTON_TEXT_VARIABLE, offsetof(TkMenuButton, textVarNameObj),
	TCL_INDEX_NONE, TK_OPTION_NULL_OK, 0, 0},
    {TK_OPTION_INDEX, "-underline", "underline", "Underline",
	TK_OPTION_UNDERLINE_DEF(TkMenuButton, underline), 0},
    {TK_OPTION_STRING, "-width", "width", "Width",
	DEF_MENUBUTTON_WIDTH, offsetof(TkMenuButton, widthObj),
	TCL_INDEX_NONE, 0, 0, 0},
    {TK_OPTION_PIXELS, "-wraplength", "wrapLength", "WrapLength",
	DEF_MENUBUTTON_WRAP_LENGTH, offsetof(TkMenuButton, wrapLengthObj),
	TCL_INDEX_NONE, 0, 0, 0},
    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0}
};

/*
 * The following tables define the menubutton widget commands and map the
 * indexes into the string tables into a single enumerated type used to
 * dispatch the scale widget command.
 */

static const char *const commandNames[] = {
    "cget", "configure", NULL
};

enum command {
    COMMAND_CGET, COMMAND_CONFIGURE
};

/*
 * Forward declarations for functions defined later in this file:
 */

static void		MenuButtonCmdDeletedProc(void *clientData);
static void		MenuButtonEventProc(void *clientData,
			    XEvent *eventPtr);
static void		MenuButtonImageProc(void *clientData,
			    int x, int y, int width, int height, int imgWidth,
			    int imgHeight);
static char *		MenuButtonTextVarProc(void *clientData,
			    Tcl_Interp *interp, const char *name1,
			    const char *name2, int flags);
static Tcl_ObjCmdProc MenuButtonWidgetObjCmd;
static int		ConfigureMenuButton(Tcl_Interp *interp,
			    TkMenuButton *mbPtr, int objc,
			    Tcl_Obj *const objv[]);
static void		DestroyMenuButton(void *memPtr);

/*
 *--------------------------------------------------------------
 *
 * Tk_MenubuttonObjCmd --
 *
 *	This function is invoked to process the "button", "label",
 *	"radiobutton", and "checkbutton" Tcl commands. See the user
 *	documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *--------------------------------------------------------------
 */

int
Tk_MenubuttonObjCmd(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,		/* Current interpreter. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    TkMenuButton *mbPtr;
    Tk_OptionTable optionTable;
    Tk_Window tkwin;

    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "pathName ?-option value ...?");
	return TCL_ERROR;
    }

    /*
     * Create the new window.
     */

    tkwin = Tk_CreateWindowFromPath(interp, Tk_MainWindow(interp),
	    Tcl_GetString(objv[1]), NULL);
    if (tkwin == NULL) {
	return TCL_ERROR;
    }

    /*
     * Create the option table for this widget class. If it has already been
     * created, the cached pointer will be returned.
     */

    optionTable = Tk_CreateOptionTable(interp, optionSpecs);

    Tk_SetClass(tkwin, "Menubutton");
    mbPtr = TkpCreateMenuButton(tkwin);

    Tk_SetClassProcs(tkwin, &menubuttonClass, mbPtr);

    /*
     * Initialize the data structure for the button.
     */

    mbPtr->tkwin = tkwin;
    mbPtr->display = Tk_Display(tkwin);
    mbPtr->interp = interp;
    mbPtr->widgetCmd = Tcl_CreateObjCommand(interp,
	    Tk_PathName(mbPtr->tkwin), MenuButtonWidgetObjCmd, mbPtr,
	    MenuButtonCmdDeletedProc);
    mbPtr->optionTable = optionTable;
    mbPtr->menuNameObj = NULL;
    mbPtr->textObj = NULL;
    mbPtr->underline = INT_MIN;
    mbPtr->textVarNameObj = NULL;
    mbPtr->bitmap = None;
    mbPtr->imageObj = NULL;
    mbPtr->image = NULL;
    mbPtr->state = STATE_NORMAL;
    mbPtr->normalBorder = NULL;
    mbPtr->activeBorder = NULL;
    mbPtr->borderWidthObj = NULL;
    mbPtr->relief = TK_RELIEF_FLAT;
    mbPtr->highlightWidthObj = 0;
    mbPtr->highlightBgColorPtr = NULL;
    mbPtr->highlightColorPtr = NULL;
    mbPtr->inset = 0;
    mbPtr->tkfont = NULL;
    mbPtr->normalFg = NULL;
    mbPtr->activeFg = NULL;
    mbPtr->disabledFg = NULL;
    mbPtr->normalTextGC = NULL;
    mbPtr->activeTextGC = NULL;
    mbPtr->gray = None;
    mbPtr->disabledGC = NULL;
    mbPtr->stippleGC = NULL;
    mbPtr->leftBearing = 0;
    mbPtr->rightBearing = 0;
    mbPtr->widthObj = NULL;
    mbPtr->heightObj = NULL;
    mbPtr->width = 0;
    mbPtr->height = 0;
    mbPtr->wrapLengthObj = 0;
    mbPtr->padXObj = NULL;
    mbPtr->padYObj = NULL;
    mbPtr->anchor = TK_ANCHOR_CENTER;
    mbPtr->justify = TK_JUSTIFY_CENTER;
    mbPtr->textLayout = NULL;
    mbPtr->indicatorOn = 0;
    mbPtr->indicatorWidth = 0;
    mbPtr->indicatorHeight = 0;
    mbPtr->direction = DIRECTION_FLUSH;
    mbPtr->cursor = NULL;
    mbPtr->takeFocusObj = NULL;
    mbPtr->flags = 0;

    Tk_CreateEventHandler(mbPtr->tkwin,
	    ExposureMask|StructureNotifyMask|FocusChangeMask,
	    MenuButtonEventProc, mbPtr);

    if (Tk_InitOptions(interp, mbPtr, optionTable, tkwin) != TCL_OK) {
	Tk_DestroyWindow(mbPtr->tkwin);
	return TCL_ERROR;
    }

    if (ConfigureMenuButton(interp, mbPtr, objc-2, objv+2) != TCL_OK) {
	Tk_DestroyWindow(mbPtr->tkwin);
	return TCL_ERROR;
    }

    Tcl_SetObjResult(interp, Tk_NewWindowObj(mbPtr->tkwin));
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * MenuButtonWidgetObjCmd --
 *
 *	This function is invoked to process the Tcl command that corresponds
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
MenuButtonWidgetObjCmd(
    void *clientData,	/* Information about button widget. */
    Tcl_Interp *interp,		/* Current interpreter. */
    int objc,			/* Number of arguments. */
    Tcl_Obj *const objv[])	/* Argument objects. */
{
    TkMenuButton *mbPtr = (TkMenuButton *)clientData;
    int result, index;
    Tcl_Obj *objPtr;

    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "option ?arg ...?");
	return TCL_ERROR;
    }
    result = Tcl_GetIndexFromObjStruct(interp, objv[1], commandNames,
	    sizeof(char *), "option", 0, &index);
    if (result != TCL_OK) {
	return result;
    }
    Tcl_Preserve(mbPtr);

    switch (index) {
    case COMMAND_CGET:
	if (objc != 3) {
	    Tcl_WrongNumArgs(interp, 1, objv, "cget option");
	    goto error;
	}

	objPtr = Tk_GetOptionValue(interp, mbPtr,
		mbPtr->optionTable, objv[2], mbPtr->tkwin);
	if (objPtr == NULL) {
	    goto error;
	}
	Tcl_SetObjResult(interp, objPtr);
	break;

    case COMMAND_CONFIGURE:
	if (objc <= 3) {
	    objPtr = Tk_GetOptionInfo(interp, mbPtr,
		    mbPtr->optionTable, (objc == 3) ? objv[2] : NULL,
		    mbPtr->tkwin);
	    if (objPtr == NULL) {
		goto error;
	    }
	    Tcl_SetObjResult(interp, objPtr);
	} else {
	    result = ConfigureMenuButton(interp, mbPtr, objc-2, objv+2);
	}
	break;
    }
    Tcl_Release(mbPtr);
    return result;

  error:
    Tcl_Release(mbPtr);
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * DestroyMenuButton --
 *
 *	This function is invoked to recycle all of the resources associated
 *	with a menubutton widget. It is invoked as a when-idle handler in
 *	order to make sure that there is no other use of the menubutton
 *	pending at the time of the deletion.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Everything associated with the widget is freed up.
 *
 *----------------------------------------------------------------------
 */

static void
DestroyMenuButton(
    void *memPtr)		/* Info about button widget. */
{
    TkMenuButton *mbPtr = (TkMenuButton *)memPtr;
    TkpDestroyMenuButton(mbPtr);

    if (mbPtr->flags & REDRAW_PENDING) {
	Tcl_CancelIdleCall(TkpDisplayMenuButton, mbPtr);
    }

    /*
     * Free up all the stuff that requires special handling, then let
     * Tk_FreeOptions handle all the standard option-related stuff.
     */

    Tcl_DeleteCommandFromToken(mbPtr->interp, mbPtr->widgetCmd);
    if (mbPtr->textVarNameObj != NULL) {
	Tcl_UntraceVar2(mbPtr->interp, Tcl_GetString(mbPtr->textVarNameObj), NULL,
		TCL_GLOBAL_ONLY|TCL_TRACE_WRITES|TCL_TRACE_UNSETS,
		MenuButtonTextVarProc, mbPtr);
    }
    if (mbPtr->image != NULL) {
	Tk_FreeImage(mbPtr->image);
    }
    if (mbPtr->normalTextGC != NULL) {
	Tk_FreeGC(mbPtr->display, mbPtr->normalTextGC);
    }
    if (mbPtr->activeTextGC != NULL) {
	Tk_FreeGC(mbPtr->display, mbPtr->activeTextGC);
    }
    if (mbPtr->disabledGC != NULL) {
	Tk_FreeGC(mbPtr->display, mbPtr->disabledGC);
    }
    if (mbPtr->stippleGC != NULL) {
	Tk_FreeGC(mbPtr->display, mbPtr->stippleGC);
    }
    if (mbPtr->gray != None) {
	Tk_FreeBitmap(mbPtr->display, mbPtr->gray);
    }
    if (mbPtr->textLayout != NULL) {
	Tk_FreeTextLayout(mbPtr->textLayout);
    }
    Tk_FreeConfigOptions(mbPtr, mbPtr->optionTable, mbPtr->tkwin);
    mbPtr->tkwin = NULL;
    Tcl_EventuallyFree(mbPtr, TCL_DYNAMIC);
}

/*
 *----------------------------------------------------------------------
 *
 * ConfigureMenuButton --
 *
 *	This function is called to process an objv/objc list, plus the Tk
 *	option database, in order to configure (or reconfigure) a menubutton
 *	widget.
 *
 * Results:
 *	The return value is a standard Tcl result. If TCL_ERROR is returned,
 *	then the interp's result contains an error message.
 *
 * Side effects:
 *	Configuration information, such as text string, colors, font, etc. get
 *	set for mbPtr; old resources get freed, if there were any. The
 *	menubutton is redisplayed.
 *
 *----------------------------------------------------------------------
 */

static int
ConfigureMenuButton(
    Tcl_Interp *interp,		/* Used for error reporting. */
    TkMenuButton *mbPtr,
				/* Information about widget; may or may not
				 * already have values for some fields. */
    int objc,			/* Number of valid entries in objv. */
    Tcl_Obj *const objv[])	/* Arguments. */
{
    Tk_SavedOptions savedOptions;
    Tcl_Obj *errorResult = NULL;
    int error;
    Tk_Image image;
    int borderWidth, highlightWidth;
    int padX, padY;

    /*
     * Eliminate any existing trace on variables monitored by the menubutton.
     */

    if (mbPtr->textVarNameObj != NULL) {
	Tcl_UntraceVar2(interp, Tcl_GetString(mbPtr->textVarNameObj), NULL,
		TCL_GLOBAL_ONLY|TCL_TRACE_WRITES|TCL_TRACE_UNSETS,
		MenuButtonTextVarProc, mbPtr);
    }

    /*
     * The following loop is potentially executed twice. During the first pass
     * configuration options get set to their new values. If there is an error
     * in this pass, we execute a second pass to restore all the options to
     * their previous values.
     */

    for (error = 0; error <= 1; error++) {
	if (!error) {
	    /*
	     * First pass: set options to new values.
	     */

	    if (Tk_SetOptions(interp, mbPtr,
		    mbPtr->optionTable, objc, objv,
		    mbPtr->tkwin, &savedOptions, NULL) != TCL_OK) {
		continue;
	    }
	} else {
	    /*
	     * Second pass: restore options to old values.
	     */

	    errorResult = Tcl_GetObjResult(interp);
	    Tcl_IncrRefCount(errorResult);
	    Tk_RestoreSavedOptions(&savedOptions);
	}

	/*
	 * A few options need special processing, such as setting the
	 * background from a 3-D border, or filling in complicated defaults
	 * that couldn't be specified to Tk_SetOptions.
	 */

	if ((mbPtr->state == STATE_ACTIVE)
		&& !Tk_StrictMotif(mbPtr->tkwin)) {
	    Tk_SetBackgroundFromBorder(mbPtr->tkwin, mbPtr->activeBorder);
	} else {
	    Tk_SetBackgroundFromBorder(mbPtr->tkwin, mbPtr->normalBorder);
	}

	Tk_GetPixelsFromObj(NULL, mbPtr->tkwin, mbPtr->borderWidthObj, &borderWidth);
	Tk_GetPixelsFromObj(NULL, mbPtr->tkwin, mbPtr->highlightWidthObj, &highlightWidth);
	Tk_GetPixelsFromObj(NULL, mbPtr->tkwin, mbPtr->padXObj, &padX);
	Tk_GetPixelsFromObj(NULL, mbPtr->tkwin, mbPtr->padYObj, &padY);
	if (borderWidth < 0) {
	    borderWidth = 0;
	    Tcl_DecrRefCount(mbPtr->borderWidthObj);
	    mbPtr->borderWidthObj = Tcl_NewIntObj(0);
	    Tcl_IncrRefCount(mbPtr->borderWidthObj);
	}
	if (highlightWidth < 0) {
	    highlightWidth = 0;
	    Tcl_DecrRefCount(mbPtr->highlightWidthObj);
	    mbPtr->highlightWidthObj = Tcl_NewIntObj(0);
	    Tcl_IncrRefCount(mbPtr->highlightWidthObj);
	}
	if (padX < 0) {
	    padX = 0;
	    Tcl_DecrRefCount(mbPtr->padXObj);
	    mbPtr->padXObj = Tcl_NewIntObj(0);
	    Tcl_IncrRefCount(mbPtr->padXObj);
	}
	if (padY < 0) {
	    padY = 0;
	    Tcl_DecrRefCount(mbPtr->padYObj);
	    mbPtr->padYObj = Tcl_NewIntObj(0);
	    Tcl_IncrRefCount(mbPtr->padYObj);
	}

	/*
	 * Get the image for the widget, if there is one. Allocate the new
	 * image before freeing the old one, so that the reference count
	 * doesn't go to zero and cause image data to be discarded.
	 */

	if (mbPtr->imageObj != NULL) {
	    image = Tk_GetImage(mbPtr->interp, mbPtr->tkwin,
		    Tcl_GetString(mbPtr->imageObj), MenuButtonImageProc, mbPtr);
	    if (image == NULL) {
		return TCL_ERROR;
	    }
	} else {
	    image = NULL;
	}
	if (mbPtr->image != NULL) {
	    Tk_FreeImage(mbPtr->image);
	}
	mbPtr->image = image;

	/*
	 * Recompute the geometry for the button.
	 */

	if ((mbPtr->bitmap != None) || (mbPtr->image != NULL)) {
	    if (Tk_GetPixelsFromObj(interp, mbPtr->tkwin, mbPtr->widthObj,
		    &mbPtr->width) != TCL_OK) {
	    widthError:
		Tcl_AddErrorInfo(interp, "\n    (processing \"-width\" option)");
		continue;
	    }
	    if (Tk_GetPixelsFromObj(interp, mbPtr->tkwin, mbPtr->heightObj,
		    &mbPtr->height) != TCL_OK) {
	    heightError:
		Tcl_AddErrorInfo(interp, "\n    (processing \"-height\" option)");
		continue;
	    }
	} else {
	    if (Tcl_GetIntFromObj(interp, mbPtr->widthObj, &mbPtr->width)
		    != TCL_OK) {
		goto widthError;
	    }
	    if (Tcl_GetIntFromObj(interp, mbPtr->heightObj, &mbPtr->height)
		    != TCL_OK) {
		goto heightError;
	    }
	}
	break;
    }

    if (!error) {
	Tk_FreeSavedOptions(&savedOptions);
    }

    if (mbPtr->textVarNameObj != NULL) {
	/*
	 * If no image or -compound is used, display the value of a variable.
	 * Set up a trace to watch for any changes in it, create the variable
	 * if it doesn't exist, and fetch its current value.
	 */
	const char *value;

	value = Tcl_GetVar2(interp, Tcl_GetString(mbPtr->textVarNameObj), NULL, TCL_GLOBAL_ONLY);
	if (value == NULL) {
	    Tcl_SetVar2(interp, Tcl_GetString(mbPtr->textVarNameObj), NULL, mbPtr->textObj ? Tcl_GetString(mbPtr->textObj) : "",
		    TCL_GLOBAL_ONLY);
	} else {
	    if (mbPtr->textObj != NULL) {
		Tcl_DecrRefCount(mbPtr->textObj);
	    }
	    mbPtr->textObj = Tcl_NewStringObj(value, TCL_INDEX_NONE);
	    Tcl_IncrRefCount(mbPtr->textObj);
	}
	Tcl_TraceVar2(interp, Tcl_GetString(mbPtr->textVarNameObj), NULL,
		TCL_GLOBAL_ONLY|TCL_TRACE_WRITES|TCL_TRACE_UNSETS,
		MenuButtonTextVarProc, mbPtr);
    }

    TkMenuButtonWorldChanged(mbPtr);
    if (error) {
	Tcl_SetObjResult(interp, errorResult);
	Tcl_DecrRefCount(errorResult);
	return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 *---------------------------------------------------------------------------
 *
 * TkMenuButtonWorldChanged --
 *
 *	This function is called when the world has changed in some way and the
 *	widget needs to recompute all its graphics contexts and determine its
 *	new geometry.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	TkMenuButton will be relayed out and redisplayed.
 *
 *---------------------------------------------------------------------------
 */

void
TkMenuButtonWorldChanged(
    void *instanceData)	/* Information about widget. */
{
    XGCValues gcValues;
    GC gc;
    unsigned long mask;
    TkMenuButton *mbPtr = (TkMenuButton *)instanceData;

    gcValues.font = Tk_FontId(mbPtr->tkfont);
    gcValues.foreground = mbPtr->normalFg->pixel;
    gcValues.background = Tk_3DBorderColor(mbPtr->normalBorder)->pixel;

    /*
     * Note: GraphicsExpose events are disabled in GC's because they're used
     * to copy stuff from an off-screen pixmap onto the screen (we know that
     * there's no problem with obscured areas).
     */

    gcValues.graphics_exposures = False;
    mask = GCForeground | GCBackground | GCFont | GCGraphicsExposures;
    gc = Tk_GetGC(mbPtr->tkwin, mask, &gcValues);
    if (mbPtr->normalTextGC != NULL) {
	Tk_FreeGC(mbPtr->display, mbPtr->normalTextGC);
    }
    mbPtr->normalTextGC = gc;

    gcValues.foreground = mbPtr->activeFg->pixel;
    gcValues.background = Tk_3DBorderColor(mbPtr->activeBorder)->pixel;
    mask = GCForeground | GCBackground | GCFont;
    gc = Tk_GetGC(mbPtr->tkwin, mask, &gcValues);
    if (mbPtr->activeTextGC != NULL) {
	Tk_FreeGC(mbPtr->display, mbPtr->activeTextGC);
    }
    mbPtr->activeTextGC = gc;

    gcValues.background = Tk_3DBorderColor(mbPtr->normalBorder)->pixel;

    /*
     * Create the GC that can be used for stippling
     */

    if (mbPtr->stippleGC == NULL) {
	gcValues.foreground = gcValues.background;
	mask = GCForeground;
	if (mbPtr->gray == None) {
	    mbPtr->gray = Tk_GetBitmap(NULL, mbPtr->tkwin, "gray50");
	}
	if (mbPtr->gray != None) {
	    gcValues.fill_style = FillStippled;
	    gcValues.stipple = mbPtr->gray;
	    mask |= GCFillStyle | GCStipple;
	}
	mbPtr->stippleGC = Tk_GetGC(mbPtr->tkwin, mask, &gcValues);
    }

    /*
     * Allocate the disabled graphics context, for drawing text in its
     * disabled state.
     */

    mask = GCForeground | GCBackground | GCFont;
    if (mbPtr->disabledFg != NULL) {
	gcValues.foreground = mbPtr->disabledFg->pixel;
    } else {
	gcValues.foreground = gcValues.background;
    }
    gc = Tk_GetGC(mbPtr->tkwin, mask, &gcValues);
    if (mbPtr->disabledGC != NULL) {
	Tk_FreeGC(mbPtr->display, mbPtr->disabledGC);
    }
    mbPtr->disabledGC = gc;

    TkpComputeMenuButtonGeometry(mbPtr);

    /*
     * Lastly, arrange for the button to be redisplayed.
     */

    if (Tk_IsMapped(mbPtr->tkwin) && !(mbPtr->flags & REDRAW_PENDING)) {
	Tcl_DoWhenIdle(TkpDisplayMenuButton, mbPtr);
	mbPtr->flags |= REDRAW_PENDING;
    }
}

/*
 *--------------------------------------------------------------
 *
 * MenuButtonEventProc --
 *
 *	This function is invoked by the Tk dispatcher for various events on
 *	buttons.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	When the window gets deleted, internal structures get cleaned up.
 *	When it gets exposed, it is redisplayed.
 *
 *--------------------------------------------------------------
 */

static void
MenuButtonEventProc(
    void *clientData,	/* Information about window. */
    XEvent *eventPtr)		/* Information about event. */
{
    TkMenuButton *mbPtr = (TkMenuButton *)clientData;
    int highlightWidth;

    if ((eventPtr->type == Expose) && (eventPtr->xexpose.count == 0)) {
	goto redraw;
    } else if (eventPtr->type == ConfigureNotify) {
	/*
	 * Must redraw after size changes, since layout could have changed and
	 * borders will need to be redrawn.
	 */

	goto redraw;
    } else if (eventPtr->type == DestroyNotify) {
	DestroyMenuButton(mbPtr);
    } else if (eventPtr->type == FocusIn) {
	if (eventPtr->xfocus.detail != NotifyInferior) {
	    mbPtr->flags |= GOT_FOCUS;
	    Tk_GetPixelsFromObj(NULL, mbPtr->tkwin, mbPtr->highlightWidthObj, &highlightWidth);
	    if (highlightWidth > 0) {
		goto redraw;
	    }
	}
    } else if (eventPtr->type == FocusOut) {
	if (eventPtr->xfocus.detail != NotifyInferior) {
	    mbPtr->flags &= ~GOT_FOCUS;
	    Tk_GetPixelsFromObj(NULL, mbPtr->tkwin, mbPtr->highlightWidthObj, &highlightWidth);
	    if (highlightWidth > 0) {
		goto redraw;
	    }
	}
    }
    return;

  redraw:
    if ((mbPtr->tkwin != NULL) && !(mbPtr->flags & REDRAW_PENDING)) {
	Tcl_DoWhenIdle(TkpDisplayMenuButton, mbPtr);
	mbPtr->flags |= REDRAW_PENDING;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * MenuButtonCmdDeletedProc --
 *
 *	This function is invoked when a widget command is deleted. If the
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
MenuButtonCmdDeletedProc(
    void *clientData)	/* Pointer to widget record for widget. */
{
    TkMenuButton *mbPtr = (TkMenuButton *)clientData;
    Tk_Window tkwin = mbPtr->tkwin;

    /*
     * This function could be invoked either because the window was destroyed
     * and the command was then deleted (in which case tkwin is NULL) or
     * because the command was deleted, and then this function destroys the
     * widget.
     */

    if (tkwin != NULL) {
	Tk_DestroyWindow(tkwin);
    }
}

/*
 *--------------------------------------------------------------
 *
 * MenuButtonTextVarProc --
 *
 *	This function is invoked when someone changes the variable whose
 *	contents are to be displayed in a menu button.
 *
 * Results:
 *	NULL is always returned.
 *
 * Side effects:
 *	The text displayed in the menu button will change to match the
 *	variable.
 *
 *--------------------------------------------------------------
 */

static char *
MenuButtonTextVarProc(
    void *clientData,	/* Information about button. */
    Tcl_Interp *interp,		/* Interpreter containing variable. */
    TCL_UNUSED(const char *),		/* Name of variable. */
    TCL_UNUSED(const char *),		/* Second part of variable name. */
    int flags)			/* Information about what happened. */
{
    TkMenuButton *mbPtr = (TkMenuButton *)clientData;
    const char *value;

    /*
     * If the variable is unset, then immediately recreate it unless the whole
     * interpreter is going away.
     */

    if (flags & TCL_TRACE_UNSETS) {
	if (!Tcl_InterpDeleted(interp) && mbPtr->textVarNameObj) {
	    void *probe = NULL;

	    do {
		probe = Tcl_VarTraceInfo(interp,
			Tcl_GetString(mbPtr->textVarNameObj),
			TCL_GLOBAL_ONLY|TCL_TRACE_WRITES|TCL_TRACE_UNSETS,
			MenuButtonTextVarProc, probe);
		if (probe == (void *)mbPtr) {
		    break;
		}
	    } while (probe);
	    if (probe) {
		/*
		 * We were able to fetch the unset trace for our
		 * textVarName, which means it is not unset and not
		 * the cause of this unset trace. Instead some outdated
		 * former variable must be, and we should ignore it.
		 */
		return NULL;
	    }
	    Tcl_SetVar2(interp, Tcl_GetString(mbPtr->textVarNameObj), NULL, mbPtr->textObj ? Tcl_GetString(mbPtr->textObj) : "",
		    TCL_GLOBAL_ONLY);
	    Tcl_TraceVar2(interp, Tcl_GetString(mbPtr->textVarNameObj), NULL,
		    TCL_GLOBAL_ONLY|TCL_TRACE_WRITES|TCL_TRACE_UNSETS,
		    MenuButtonTextVarProc, clientData);
	}
	return NULL;
    }

    value = Tcl_GetVar2(interp, Tcl_GetString(mbPtr->textVarNameObj), NULL, TCL_GLOBAL_ONLY);
    if (value == NULL) {
	value = "";
    }
    if (mbPtr->textObj != NULL) {
	Tcl_DecrRefCount(mbPtr->textObj);
    }
    mbPtr->textObj= Tcl_NewStringObj(value, TCL_INDEX_NONE);
	Tcl_IncrRefCount(mbPtr->textObj);
    TkpComputeMenuButtonGeometry(mbPtr);

    if ((mbPtr->tkwin != NULL) && Tk_IsMapped(mbPtr->tkwin)
	    && !(mbPtr->flags & REDRAW_PENDING)) {
	Tcl_DoWhenIdle(TkpDisplayMenuButton, mbPtr);
	mbPtr->flags |= REDRAW_PENDING;
    }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * MenuButtonImageProc --
 *
 *	This function is invoked by the image code whenever the manager for an
 *	image does something that affects the size of contents of an image
 *	displayed in a button.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Arranges for the button to get redisplayed.
 *
 *----------------------------------------------------------------------
 */

static void
MenuButtonImageProc(
    void *clientData,	/* Pointer to widget record. */
    TCL_UNUSED(int),	/* x, Upper left pixel (within image) that must */
    TCL_UNUSED(int),	/* y, be redisplayed. */
    TCL_UNUSED(int),	/* width, Dimensions of area to redisplay (may be <= */
    TCL_UNUSED(int),	/* height, 0). */
    TCL_UNUSED(int),	/* imgWidth, New dimensions of image. */
    TCL_UNUSED(int))	/* imgHeight */
{
    TkMenuButton *mbPtr = (TkMenuButton *)clientData;

    if (mbPtr->tkwin != NULL) {
	TkpComputeMenuButtonGeometry(mbPtr);
	if (Tk_IsMapped(mbPtr->tkwin) && !(mbPtr->flags & REDRAW_PENDING)) {
	    Tcl_DoWhenIdle(TkpDisplayMenuButton, mbPtr);
	    mbPtr->flags |= REDRAW_PENDING;
	}
    }
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
