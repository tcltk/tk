/*
 * Copyright © 2003 Joe English
 *
 * Ttk package: initialization routine and miscellaneous utilities.
 */

#include "tkInt.h"
#include "ttkTheme.h"
#include "ttkWidget.h"

/*
 * Legal values for the button -default option.
 * See also: enum Ttk_ButtonDefaultState.
 */
const char *const ttkDefaultStrings[] = {
    "active", "disabled", "normal", NULL
};

int Ttk_GetButtonDefaultStateFromObj(
    Tcl_Interp *interp, Tcl_Obj *objPtr, Ttk_ButtonDefaultState *statePtr)
{
    int state = (int)TTK_BUTTON_DEFAULT_DISABLED;
    int result = Tcl_GetIndexFromObj(interp, objPtr, ttkDefaultStrings,
	    "default state", 0, &state);

    *statePtr = (Ttk_ButtonDefaultState)state;
    return result;
}

/*
 * Legal values for the -compound option.
 * See also: enum Ttk_Compound.
 */
const char *const ttkCompoundStrings[] = {
    "none", "text", "image", "center",
    "top", "bottom", "left", "right", NULL
};

int Ttk_GetCompoundFromObj(
    Tcl_Interp *interp, Tcl_Obj *objPtr, Ttk_Compound *compoundPtr)
{
    int compound = (int)TTK_COMPOUND_NONE;
    int result = Tcl_GetIndexFromObj(interp, objPtr, ttkCompoundStrings,
	    "compound layout", 0, &compound);

    *compoundPtr = (Ttk_Compound)compound;
    return result;
}

/*
 * Legal values for the -orient option.
 * See also: enum Ttk_Orient.
 */
const char *const ttkOrientStrings[] = {
    "horizontal", "vertical", NULL
};

int Ttk_GetOrientFromObj(
    Tcl_Interp *interp, Tcl_Obj *objPtr, Ttk_Orient *resultPtr)
{
    int orient = (int)TTK_ORIENT_HORIZONTAL;
    int result = Tcl_GetIndexFromObj(interp, objPtr, ttkOrientStrings,
	    "orientation", 0, &orient);

    *resultPtr = (Ttk_Orient)orient;
    return result;
}

/*
 * Recognized values for the -state compatibility option.
 * Other options are accepted and interpreted as synonyms for "normal".
 */
static const char *const ttkStateStrings[] = {
    "active", "disabled", "normal", "readonly", NULL
};
enum {
    TTK_COMPAT_STATE_ACTIVE,
    TTK_COMPAT_STATE_DISABLED,
    TTK_COMPAT_STATE_NORMAL,
    TTK_COMPAT_STATE_READONLY
};

/* TtkCheckStateOption --
 *	Handle -state compatibility option.
 *
 *	NOTE: setting -state disabled / -state enabled affects the
 *	widget state, but the internal widget state does *not* affect
 *	the value of the -state option.
 *	This option is present for compatibility only.
 */
void TtkCheckStateOption(WidgetCore *corePtr, Tcl_Obj *objPtr)
{
    int stateOption = TTK_COMPAT_STATE_NORMAL;
    unsigned all = TTK_STATE_DISABLED|TTK_STATE_READONLY|TTK_STATE_ACTIVE;
#   define SETFLAGS(f) TtkWidgetChangeState(corePtr, f, all^f)

    Tcl_GetIndexFromObj(NULL, objPtr, ttkStateStrings,
	    "", 0, &stateOption);
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

/* TtkEnumerateOptions, TtkGetOptionValue --
 *	Common factors for data accessor commands.
 */
int TtkEnumerateOptions(
    Tcl_Interp *interp, void *recordPtr, const Tk_OptionSpec *specPtr,
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
	    specPtr = (const Tk_OptionSpec *)specPtr->clientData;
	}
    }
    Tcl_SetObjResult(interp, result);
    return TCL_OK;
}

int TtkGetOptionValue(
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
const Tk_OptionSpec ttkCoreOptionSpecs[] =
{
    {TK_OPTION_CURSOR, "-cursor", "cursor", "Cursor", NULL,
	offsetof(WidgetCore, cursorObj), TCL_INDEX_NONE, TK_OPTION_NULL_OK,0,0 },
    {TK_OPTION_STRING, "-style", "style", "Style", "",
	offsetof(WidgetCore,styleObj), TCL_INDEX_NONE, 0,0,STYLE_CHANGED},
    {TK_OPTION_STRING, "-class", "", "", NULL,
	offsetof(WidgetCore,classObj), TCL_INDEX_NONE, 0,0,READONLY_OPTION},
    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, 0, 0}
};

/*------------------------------------------------------------------------
 * +++ Initialization: elements and element factories.
 */

MODULE_SCOPE void TtkElements_Init(Tcl_Interp *);
MODULE_SCOPE void TtkLabel_Init(Tcl_Interp *);
MODULE_SCOPE void TtkImage_Init(Tcl_Interp *);

static void RegisterElements(Tcl_Interp *interp)
{
    TtkElements_Init(interp);
    TtkLabel_Init(interp);
    TtkImage_Init(interp);
}

/*------------------------------------------------------------------------
 * +++ Initialization: Widget definitions.
 */

MODULE_SCOPE void TtkButton_Init(Tcl_Interp *);
MODULE_SCOPE void TtkEntry_Init(Tcl_Interp *);
MODULE_SCOPE void TtkFrame_Init(Tcl_Interp *);
MODULE_SCOPE void TtkNotebook_Init(Tcl_Interp *);
MODULE_SCOPE void TtkPanedwindow_Init(Tcl_Interp *);
MODULE_SCOPE void TtkProgressbar_Init(Tcl_Interp *);
MODULE_SCOPE void TtkScale_Init(Tcl_Interp *);
MODULE_SCOPE void TtkScrollbar_Init(Tcl_Interp *);
MODULE_SCOPE void TtkSeparator_Init(Tcl_Interp *);
MODULE_SCOPE void TtkTreeview_Init(Tcl_Interp *);

#ifdef TTK_SQUARE_WIDGET
MODULE_SCOPE int TtkSquareWidget_Init(Tcl_Interp *);
#endif

static void RegisterWidgets(Tcl_Interp *interp)
{
    TtkButton_Init(interp);
    TtkEntry_Init(interp);
    TtkFrame_Init(interp);
    TtkNotebook_Init(interp);
    TtkPanedwindow_Init(interp);
    TtkProgressbar_Init(interp);
    TtkScale_Init(interp);
    TtkScrollbar_Init(interp);
    TtkSeparator_Init(interp);
    TtkTreeview_Init(interp);
#ifdef TTK_SQUARE_WIDGET
    TtkSquareWidget_Init(interp);
#endif
}

/*------------------------------------------------------------------------
 * +++ Initialization: Built-in themes.
 */

MODULE_SCOPE int TtkAltTheme_Init(Tcl_Interp *);
MODULE_SCOPE int TtkClassicTheme_Init(Tcl_Interp *);
MODULE_SCOPE int TtkClamTheme_Init(Tcl_Interp *);

static void RegisterThemes(Tcl_Interp *interp)
{

    TtkAltTheme_Init(interp);
    TtkClassicTheme_Init(interp);
    TtkClamTheme_Init(interp);
}

/*
 * Ttk initialization.
 */

extern const TtkStubs ttkStubs;

MODULE_SCOPE int
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

#ifndef TK_NO_DEPRECATED
    Tcl_PkgProvideEx(interp, "Ttk", TTK_PATCH_LEVEL, (void *)&ttkStubs);
#endif
    Tcl_PkgProvideEx(interp, "ttk", TTK_PATCH_LEVEL, (void *)&ttkStubs);

    return TCL_OK;
}

/*EOF*/
