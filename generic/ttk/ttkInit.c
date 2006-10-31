/* $Id: ttkInit.c,v 1.1 2006/10/31 01:42:26 hobbs Exp $
 * Copyright (c) 2003, Joe English
 *
 * Ttk package: initialization routine and miscellaneous utilities.
 */

#include <string.h>
#include <tk.h>
#include "ttkTheme.h"
#include "ttkWidget.h"

/*
 * Legal values for the button -default option.
 * See also: enum Ttk_ButtonDefaultState in ttkTheme.h.
 */
CONST char *TTKDefaultStrings[] = {
    "normal", "active", "disabled", NULL
};

int Ttk_GetButtonDefaultStateFromObj(
    Tcl_Interp *interp, Tcl_Obj *objPtr, int *statePtr)
{
    *statePtr = TTK_BUTTON_DEFAULT_DISABLED;
    return Tcl_GetIndexFromObj(interp, objPtr,
	    TTKDefaultStrings, "default state", 0, statePtr);
}

/*
 * Legal values for the -compound option.
 * See also: enum Ttk_Compound in ttkTheme.h
 */
const char *TTKCompoundStrings[] = {
    "none", "text", "image", "center",
    "top", "bottom", "left", "right", NULL
};

int Ttk_GetCompoundFromObj(
    Tcl_Interp *interp, Tcl_Obj *objPtr, int *statePtr)
{
    *statePtr = TTK_COMPOUND_NONE;
    return Tcl_GetIndexFromObj(interp, objPtr,
	    TTKCompoundStrings, "compound layout", 0, statePtr);
}

/*
 * Legal values for the -orient option.
 * See also: enum TTK_ORIENT in ttkTheme.h
 */
CONST char *TTKOrientStrings[] = {
    "horizontal", "vertical", NULL
};

int Ttk_GetOrientFromObj(
    Tcl_Interp *interp, Tcl_Obj *objPtr, int *resultPtr)
{
    *resultPtr = TTK_ORIENT_HORIZONTAL;
    return Tcl_GetIndexFromObj(interp, objPtr,
	    TTKOrientStrings, "orientation", 0, resultPtr);
}

/*
 * Recognized values for the -state compatibility option.
 * Other options are accepted and interpreted as synonyms for "normal".
 */
static const char *TTKStateStrings[] = {
    "normal", "readonly", "disabled", "active", NULL
};
enum { 
    TTK_COMPAT_STATE_NORMAL,
    TTK_COMPAT_STATE_READONLY,
    TTK_COMPAT_STATE_DISABLED,
    TTK_COMPAT_STATE_ACTIVE
};

/* CheckStateOption -- 
 * 	Handle -state compatibility option.
 *
 *	NOTE: setting -state disabled / -state enabled affects the 
 *	widget state, but the internal widget state does *not* affect 
 *	the value of the -state option.
 *	This option is present for compatibility only.
 */
void CheckStateOption(WidgetCore *corePtr, Tcl_Obj *objPtr)
{
    int stateOption = TTK_COMPAT_STATE_NORMAL;
    unsigned all = TTK_STATE_DISABLED|TTK_STATE_READONLY|TTK_STATE_ACTIVE;
#   define SETFLAGS(f) WidgetChangeState(corePtr, f, all^f)

    (void)Tcl_GetIndexFromObj(NULL,objPtr,TTKStateStrings,"",0,&stateOption);
    switch (stateOption) {
	case TTK_COMPAT_STATE_NORMAL:
	default:
	    SETFLAGS(0);
	    break;
	case TTK_COMPAT_STATE_READONLY:
	    SETFLAGS(TTK_STATE_READONLY);
	    break;
	case TTK_COMPAT_STATE_DISABLED:
	    SETFLAGS(TTK_STATE_DISABLED);
	    break;
	case TTK_COMPAT_STATE_ACTIVE:
	    SETFLAGS(TTK_STATE_ACTIVE);
	    break;
    }
#   undef SETFLAGS
}

/* SendVirtualEvent --
 * 	Send a virtual event notification to the specified target window.
 * 	Equivalent to "event generate $tgtWindow <<$eventName>>"
 *
 * 	Note that we use Tk_QueueWindowEvent, not Tk_HandleEvent,
 * 	so this routine does not reenter the interpreter.
 */
void SendVirtualEvent(Tk_Window tgtWin, const char *eventName)
{
    XEvent event;

    memset(&event, 0, sizeof(event));
    event.xany.type = VirtualEvent;
    event.xany.serial = NextRequest(Tk_Display(tgtWin));
    event.xany.send_event = False;
    event.xany.window = Tk_WindowId(tgtWin);
    event.xany.display = Tk_Display(tgtWin);
    ((XVirtualEvent *) &event)->name = Tk_GetUid(eventName);

    Tk_QueueWindowEvent(&event, TCL_QUEUE_TAIL);
}

/* EnumerateOptions, GetOptionValue --
 *	Common factors for data accessor commands.
 */
int EnumerateOptions(
    Tcl_Interp *interp, void *recordPtr, Tk_OptionSpec *specPtr,
    Tk_OptionTable optionTable, Tk_Window tkwin)
{
    Tcl_Obj *result = Tcl_NewListObj(0,0);
    while (specPtr->type != TK_OPTION_END)
    {
	Tcl_Obj *optionName = Tcl_NewStringObj(specPtr->optionName, -1);
	Tcl_Obj *optionValue =
	    Tk_GetOptionValue(interp,recordPtr,optionTable,optionName,tkwin);
	if (optionValue) {
	    Tcl_ListObjAppendElement(interp, result, optionName);
	    Tcl_ListObjAppendElement(interp, result, optionValue);
	}
	++specPtr;

	if (specPtr->type == TK_OPTION_END && specPtr->clientData != NULL) {
	    /* Chain to next option spec array: */
	    specPtr = specPtr->clientData;
	}
    }
    Tcl_SetObjResult(interp, result);
    return TCL_OK;
}

int GetOptionValue(
    Tcl_Interp *interp, void *recordPtr, Tcl_Obj *optionName,
    Tk_OptionTable optionTable, Tk_Window tkwin)
{
    Tcl_Obj *result =
	Tk_GetOptionValue(interp,recordPtr,optionTable,optionName,tkwin);
    if (result) {
	Tcl_SetObjResult(interp, result);
	return TCL_OK;
    }
    return TCL_ERROR;
}


/*------------------------------------------------------------------------
 * Core Option specifications:
 * type name dbName dbClass default objOffset intOffset flags clientData mask
 */

/* public */ 
Tk_OptionSpec CoreOptionSpecs[] =
{
    {TK_OPTION_STRING, "-takefocus", "takeFocus", "TakeFocus",
	"", Tk_Offset(WidgetCore, takeFocusPtr), -1, 0,0,0 },
    {TK_OPTION_CURSOR, "-cursor", "cursor", "Cursor", NULL,
	Tk_Offset(WidgetCore, cursorObj), -1, TK_OPTION_NULL_OK,0,0 },
    {TK_OPTION_STRING, "-style", "style", "Style", "",
	Tk_Offset(WidgetCore,styleObj), -1, 0,0,STYLE_CHANGED},
    {TK_OPTION_STRING, "-class", "", "", NULL,
	Tk_Offset(WidgetCore,classObj), -1, 0,0,READONLY_OPTION},
    {TK_OPTION_END}
};

/*------------------------------------------------------------------------
 * +++ Widget definitions.
 */

extern WidgetSpec FrameWidgetSpec;
extern WidgetSpec LabelframeWidgetSpec;
extern WidgetSpec LabelWidgetSpec;
extern WidgetSpec ButtonWidgetSpec;
extern WidgetSpec CheckbuttonWidgetSpec;
extern WidgetSpec RadiobuttonWidgetSpec;
extern WidgetSpec MenubuttonWidgetSpec;
extern WidgetSpec ScrollbarWidgetSpec;
extern WidgetSpec ScaleWidgetSpec;
extern WidgetSpec SeparatorWidgetSpec;
extern WidgetSpec SizegripWidgetSpec;

extern void Progressbar_Init(Tcl_Interp *);
extern void Notebook_Init(Tcl_Interp *);
extern void EntryWidget_Init(Tcl_Interp *);
extern void Treeview_Init(Tcl_Interp *);
extern void Paned_Init(Tcl_Interp *);

#ifdef TTK_SQUARE_WIDGET
extern void SquareWidget_Init(Tcl_Interp *);
#endif

static void RegisterWidgets(Tcl_Interp *interp)
{
    RegisterWidget(interp, "::ttk::frame", &FrameWidgetSpec);
    RegisterWidget(interp, "::ttk::labelframe", &LabelframeWidgetSpec);
    RegisterWidget(interp, "::ttk::label", &LabelWidgetSpec);
    RegisterWidget(interp, "::ttk::button", &ButtonWidgetSpec);
    RegisterWidget(interp, "::ttk::checkbutton", &CheckbuttonWidgetSpec);
    RegisterWidget(interp, "::ttk::radiobutton", &RadiobuttonWidgetSpec);
    RegisterWidget(interp, "::ttk::menubutton", &MenubuttonWidgetSpec);
    RegisterWidget(interp, "::ttk::scrollbar", &ScrollbarWidgetSpec);
    RegisterWidget(interp, "::ttk::scale", &ScaleWidgetSpec);
    RegisterWidget(interp, "::ttk::separator", &SeparatorWidgetSpec);
    RegisterWidget(interp, "::ttk::sizegrip", &SizegripWidgetSpec);
    Notebook_Init(interp);
    EntryWidget_Init(interp);
    Progressbar_Init(interp);
    Paned_Init(interp);
#ifdef TTK_TREEVIEW_WIDGET
    Treeview_Init(interp);
#endif

#ifdef TTK_SQUARE_WIDGET
    SquareWidget_Init(interp);
#endif
}

/*------------------------------------------------------------------------
 * +++ Built-in themes.
 */

extern int AltTheme_Init(Tcl_Interp *);
extern int ClassicTheme_Init(Tcl_Interp *);
extern int ClamTheme_Init(Tcl_Interp *);

extern int Ttk_ImageInit(Tcl_Interp *);

static void RegisterThemes(Tcl_Interp *interp)
{
    Ttk_ImageInit(interp);	/* not really a theme... */
    AltTheme_Init(interp);
    ClassicTheme_Init(interp);
    ClamTheme_Init(interp);
}

/*
 * Ttk initialization.
 */

extern TtkStubs ttkStubs;

int DLLEXPORT
Ttk_Init(Tcl_Interp *interp)
{
    /*
     * This will be run for both safe and regular interp init.
     * Use Tcl_IsSafe if necessary to not initialize unsafe bits.
     */
    Ttk_StylePkgInit(interp);

    RegisterElements(interp);
    RegisterWidgets(interp);
    RegisterThemes(interp);

    Ttk_PlatformInit(interp);

#if 0
    Tcl_PkgProvideEx(interp, "Ttk", TTK_PATCH_LEVEL, (void*)&ttkStubs);
#endif

    return TCL_OK;
}

/*EOF*/
