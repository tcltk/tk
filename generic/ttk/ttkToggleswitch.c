/*
 * Copyright © 2025 Csaba Nemethi <csaba.nemethi@t-online.de>
 *
 * ttk::toggleswitch widget.
 */

#include "tkInt.h"
#include "ttkTheme.h"
#include "ttkWidget.h"

/*
 * Tglswitch widget record
 */
typedef struct
{
    /* widget options */
    Tcl_Obj *commandObj;
    Tcl_Obj *offValueObj;
    Tcl_Obj *onValueObj;
    Tcl_Obj *sizeObj;
    Tcl_Obj *variableObj;

    /* internal state */
    Tcl_Obj *minValObj;		/* minimum value */
    Tcl_Obj *maxValObj;		/* maximum value */
    Tcl_Obj *curValObj;		/* current value */
    Ttk_TraceHandle *varTrace;
    double minVal, maxVal;
} TglswitchPart;

typedef struct
{
    WidgetCore	  core;
    TglswitchPart tglsw;
} Tglswitch;

static const char *const sizeStrings[] = { "1", "2", "3", NULL };

static const Tk_OptionSpec TglswitchOptionSpecs[] =
{
    {TK_OPTION_STRING, "-command", "command", "Command", "",
	offsetof(Tglswitch, tglsw.commandObj), TCL_INDEX_NONE,
	0, 0, 0},
    {TK_OPTION_STRING, "-offvalue", "offValue", "OffValue", "0",
	offsetof(Tglswitch, tglsw.offValueObj), TCL_INDEX_NONE,
	0, 0, 0},
    {TK_OPTION_STRING, "-onvalue", "onValue", "OnValue", "1",
	offsetof(Tglswitch, tglsw.onValueObj), TCL_INDEX_NONE,
	0, 0, 0},
    {TK_OPTION_STRING_TABLE, "-size", "size", "Size", "2",
	offsetof(Tglswitch, tglsw.sizeObj), TCL_INDEX_NONE,
	0, sizeStrings, GEOMETRY_CHANGED},
    {TK_OPTION_STRING, "-variable", "variable", "Variable", NULL,
	offsetof(Tglswitch, tglsw.variableObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK, 0, 0},

    WIDGET_TAKEFOCUS_TRUE,
    WIDGET_INHERIT_OPTIONS(ttkCoreOptionSpecs)
};

/*
 * TglswitchVariableChanged --
 *	Variable trace procedure for the ttk::toggleswitch -variable option.
 *	Updates the ttk::toggleswitch widget's switch state.
 */
static void TglswitchVariableChanged(void *clientData, const char *value)
{
    Tglswitch *tglswPtr = (Tglswitch *)clientData;

    if (WidgetDestroyed(&tglswPtr->core)) {
	return;
    }

    if (value == NULL) {
	TtkWidgetChangeState(&tglswPtr->core, TTK_STATE_INVALID, 0);
    } else {
	Tcl_DecrRefCount(tglswPtr->tglsw.curValObj);
	if (!strcmp(value, Tcl_GetString(tglswPtr->tglsw.onValueObj))) {
	    TtkWidgetChangeState(&tglswPtr->core, TTK_STATE_SELECTED, 0);
	    tglswPtr->tglsw.curValObj = tglswPtr->tglsw.maxValObj;
	} else {
	    TtkWidgetChangeState(&tglswPtr->core, 0, TTK_STATE_SELECTED);
	    tglswPtr->tglsw.curValObj = tglswPtr->tglsw.minValObj;
	}
	Tcl_IncrRefCount(tglswPtr->tglsw.curValObj);

	TtkWidgetChangeState(&tglswPtr->core, 0, TTK_STATE_INVALID);
    }

    TtkRedisplayWidget(&tglswPtr->core);
}

/*
 * TglswitchInitialize --
 *	ttk::toggleswitch widget initialization hook.
 */
static void TglswitchInitialize(Tcl_Interp *interp, void *recordPtr)
{
    Tglswitch *tglswPtr = (Tglswitch *)recordPtr;

    /*
     * Create the *Tglswitch*.trough and *Tglswitch*.slider
     * elements for the Toggleswitch* styles if necessary
     */
    int code = Tcl_EvalEx(interp, "ttk::toggleswitch::CondMakeElements",
	    TCL_INDEX_NONE, TCL_EVAL_GLOBAL);
    if (code != TCL_OK) {
	Tcl_BackgroundException(interp, code);
    }

    /*
     * Initialize the minimum, maximum, and current values
     */

    tglswPtr->tglsw.minVal = 0.0;
    tglswPtr->tglsw.minValObj = Tcl_NewDoubleObj(tglswPtr->tglsw.minVal);
    Tcl_IncrRefCount(tglswPtr->tglsw.minValObj);

    tglswPtr->tglsw.maxVal = 20.0;
    tglswPtr->tglsw.maxValObj = Tcl_NewDoubleObj(tglswPtr->tglsw.maxVal);
    Tcl_IncrRefCount(tglswPtr->tglsw.maxValObj);

    tglswPtr->tglsw.curValObj = Tcl_NewDoubleObj(0.0);
    Tcl_IncrRefCount(tglswPtr->tglsw.curValObj);

    /*
     * Set the -variable option to the widget's path name
     */
    tglswPtr->tglsw.variableObj =
	    Tcl_NewStringObj(Tk_PathName(tglswPtr->core.tkwin), -1);
    Tcl_IncrRefCount(tglswPtr->tglsw.variableObj);

    TtkTrackElementState(&tglswPtr->core);
}

/*
 * TglswitchCleanup --
 *	Cleanup hook.
 */
static void TglswitchCleanup(void *recordPtr)
{
    Tglswitch *tglswPtr = (Tglswitch *)recordPtr;

    if (tglswPtr->tglsw.varTrace) {
	Ttk_UntraceVariable(tglswPtr->tglsw.varTrace);
	tglswPtr->tglsw.varTrace = 0;
    }
}

/*
 * TglswitchConfigure --
 *	Configuration hook.
 */
static int TglswitchConfigure(Tcl_Interp *interp, void *recordPtr, int mask)
{
    Tglswitch *tglswPtr = (Tglswitch *)recordPtr;
    Tcl_Obj *variableObj = tglswPtr->tglsw.variableObj;
    Ttk_TraceHandle *varTrace = NULL;

    if (mask & GEOMETRY_CHANGED) {
	/*
	 * Processing the "-size" option:  Set the "-style" option to
	 * "(*.)Toggleswitch{1|2|3}" if its value is of the same form.
	 */

	const char *styleName = 0, *lastDot = 0, *nameTail = 0;

	if (tglswPtr->core.styleObj) {
	    styleName = Tcl_GetString(tglswPtr->core.styleObj);
	}
	if (!styleName || *styleName == '\0') {
	    styleName = "Toggleswitch2";
	}
	lastDot = strrchr(styleName, '.');
	nameTail = lastDot ? lastDot + 1 : styleName;

	if (!strcmp(nameTail, "Toggleswitch1")
		|| !strcmp(nameTail, "Toggleswitch2")
		|| !strcmp(nameTail, "Toggleswitch3")) {
	    size_t length = strlen(styleName);
	    char *styleName2 = (char *)Tcl_Alloc(length + 1);
	    const char *sizeStr = Tcl_GetString(tglswPtr->tglsw.sizeObj);

	    memcpy(styleName2, styleName, length + 1);
	    styleName2[length-1] = *sizeStr;

	    Tcl_DecrRefCount(tglswPtr->core.styleObj);
	    tglswPtr->core.styleObj = Tcl_NewStringObj(styleName2, -1);
	    Tcl_IncrRefCount(tglswPtr->core.styleObj);

	    Tcl_Free(styleName2);

	    /*
	     * Update the layout according to the new style
	     */
	    TtkCoreConfigure(interp, recordPtr, STYLE_CHANGED);
	}
    } else if (mask & STYLE_CHANGED) {		/* intentionally "else if" */
	/*
	 * Processing the "-style" option:  Set the "-size" option
	 * to "1|2|3" if the style is "(*.)Toggleswitch{1|2|3}"
	 */

	const char *sizeStr = 0;
	const char *styleName = Tcl_GetString(tglswPtr->core.styleObj);
	const char *lastDot = strrchr(styleName, '.');
	const char *nameTail = lastDot ? lastDot + 1 : styleName;

	if (!strcmp(nameTail, "Toggleswitch1")) {
	    sizeStr = "1";
	} else if (!strcmp(nameTail, "Toggleswitch2")) {
	    sizeStr = "2";
	} else if (!strcmp(nameTail, "Toggleswitch3")) {
	    sizeStr = "3";
	}

	if (sizeStr) {
	    Tcl_DecrRefCount(tglswPtr->tglsw.sizeObj);
	    tglswPtr->tglsw.sizeObj = Tcl_NewStringObj(sizeStr, -1);
	    Tcl_IncrRefCount(tglswPtr->tglsw.sizeObj);
	}
    }

    if (!TkObjIsEmpty(variableObj)) {
	varTrace = Ttk_TraceVariable(interp, variableObj,
		TglswitchVariableChanged, recordPtr);
	if (!varTrace) {
	    return TCL_ERROR;
	}
    }

    if (TtkCoreConfigure(interp, recordPtr, mask) != TCL_OK) {
	Ttk_UntraceVariable(varTrace);
	return TCL_ERROR;
    }

    if (tglswPtr->tglsw.varTrace) {
	Ttk_UntraceVariable(tglswPtr->tglsw.varTrace);
    }
    tglswPtr->tglsw.varTrace = varTrace;

    return TCL_OK;
}

/*
 * TglswitchPostConfigure --
 *	Post-configuration hook.
 */
static int TglswitchPostConfigure(
    TCL_UNUSED(Tcl_Interp *),
    void *recordPtr,
    TCL_UNUSED(int))
{
    Tglswitch *tglswPtr = (Tglswitch *)recordPtr;
    int status = TCL_OK;

    if (tglswPtr->tglsw.varTrace) {
	status = Ttk_FireTrace(tglswPtr->tglsw.varTrace);
	if (WidgetDestroyed(&tglswPtr->core)) {
	    return TCL_ERROR;
	}
    }

    return status;
}

/*
 * TglswitchGetLayout --
 *	getLayout hook.
 */
static Ttk_Layout TglswitchGetLayout(
    Tcl_Interp *interp, Ttk_Theme themePtr, void *recordPtr)
{
    Tglswitch *tglswPtr = (Tglswitch *)recordPtr;
    const char *styleName = 0;
    Tcl_DString dsStyleName;
    Ttk_Layout layout;

    Tcl_DStringInit(&dsStyleName);

    if (tglswPtr->core.styleObj) {
	styleName = Tcl_GetString(tglswPtr->core.styleObj);
	Tcl_DStringAppend(&dsStyleName, styleName, TCL_INDEX_NONE);
    }
    if (!styleName || *styleName == '\0') {
	const char *sizeStr = Tcl_GetString(tglswPtr->tglsw.sizeObj);

	styleName = tglswPtr->core.widgetSpec->className;
	Tcl_DStringAppend(&dsStyleName, styleName, TCL_INDEX_NONE);
	Tcl_DStringAppend(&dsStyleName, sizeStr, TCL_INDEX_NONE);
    }

    layout = Ttk_CreateLayout(interp, themePtr, Tcl_DStringValue(&dsStyleName),
	    recordPtr, tglswPtr->core.optionTable, tglswPtr->core.tkwin);

    Tcl_DStringFree(&dsStyleName);

    return layout;
}

/*
 * TroughRange --
 *	Returns the value area of the trough element, adjusted for slider size.
 */
static Ttk_Box TroughRange(Tglswitch *tglswPtr)
{
    Ttk_Box troughBox = Ttk_ClientRegion(tglswPtr->core.layout, "trough");
    Ttk_Element slider = Ttk_FindElement(tglswPtr->core.layout, "slider");

    if (slider) {
	Ttk_Box sliderBox = Ttk_ElementParcel(slider);
	troughBox.x += sliderBox.width / 2;
	troughBox.width -= sliderBox.width;
    }

    return troughBox;
}

/*
 * ValueToFraction --
 *	Returns the fraction corresponding to a given value.
 */
static double ValueToFraction(Tglswitch *tglswPtr, double value)
{
    double minVal = tglswPtr->tglsw.minVal;
    double maxVal = tglswPtr->tglsw.maxVal;
    double fraction = (value - minVal) / (maxVal - minVal);

    return fraction < 0 ? 0 : fraction > 1 ? 1 : fraction;
}

/*
 * ValueToPoint --
 *	Returns the x coordinate corresponding to a given value.
 */
static int ValueToPoint(Tglswitch *tglswPtr, double value)
{
    Ttk_Box troughBox = TroughRange(tglswPtr);
    double fraction = ValueToFraction(tglswPtr, value);

    return troughBox.x + (int)(fraction * troughBox.width);
}

/*
 * PointToValue --
 *	Returns the value corresponding to a given x coordinate.
 */
static double PointToValue(Tglswitch *tglswPtr, int x)
{
    Ttk_Box troughBox = TroughRange(tglswPtr);
    double minVal = tglswPtr->tglsw.minVal;
    double maxVal = tglswPtr->tglsw.maxVal;
    double value = 0.0, fraction;

    Tcl_GetDoubleFromObj(NULL, tglswPtr->tglsw.curValObj, &value);
    if (troughBox.width <= 0) {
	return value;
    }

    fraction = (double)(x - troughBox.x) / (double)troughBox.width;
    fraction = fraction < 0 ? 0 : fraction > 1 ? 1 : fraction;

    return minVal + fraction * (maxVal - minVal);
}

/*
 * TglswitchDoLayout --
 */
static void TglswitchDoLayout(void *clientData)
{
    WidgetCore *corePtr = (WidgetCore *)clientData;
    Ttk_Element slider = Ttk_FindElement(corePtr->layout, "slider");

    Ttk_PlaceLayout(corePtr->layout, corePtr->state,
	    Ttk_WinBox(corePtr->tkwin));

    /*
     * Adjust the slider position
     */
    if (slider) {
	Tglswitch *tglswPtr = (Tglswitch *)clientData;
	Ttk_Box troughBox = Ttk_ClientRegion(tglswPtr->core.layout, "trough");
	Ttk_Box sliderBox = Ttk_ElementParcel(slider);
	double value = 0.0;
	double fraction;
	int range;

	Tcl_GetDoubleFromObj(NULL, tglswPtr->tglsw.curValObj, &value);
	fraction = ValueToFraction(tglswPtr, value);
	range = troughBox.width - sliderBox.width;

	sliderBox.x += (int)(fraction * range);
	Ttk_PlaceElement(corePtr->layout, slider, sliderBox);
    }
}

/*
 * $toggleswitch get ?min|max|$x? --
 *	Returns the ttk::toggleswitch widget's current/minimum/maximum value,
 *	or the value corresponding to $x.
 */
static int TglswitchGetCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Tglswitch *tglswPtr = (Tglswitch *)recordPtr;
    char *arg2 = NULL;
    int x, res = TCL_OK;
    double value = 0.0;

    if (objc == 2) {
	Tcl_SetObjResult(interp, tglswPtr->tglsw.curValObj);
    } else if (objc == 3) {
	arg2 = Tcl_GetString(objv[2]);
	if (!strcmp(arg2, "min")) {
	    Tcl_SetObjResult(interp, tglswPtr->tglsw.minValObj);
	} else if (!strcmp(arg2, "max")) {
	    Tcl_SetObjResult(interp, tglswPtr->tglsw.maxValObj);
	} else {
	    res = Tcl_GetIntFromObj(interp, objv[2], &x);
	    if (res == TCL_OK) {
		value = PointToValue(tglswPtr, x);
		Tcl_SetObjResult(interp, Tcl_NewDoubleObj(value));
	    }
	}
    } else {
	Tcl_WrongNumArgs(interp, 1, objv, "get ?min|max|x?");
	return TCL_ERROR;
    }

    return res;
}

/*
 * $toggleswitch set $newValue
 *	Sets the ttk::toggleswitch widget's value to $newValue.
 */
static int TglswitchSetCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Tglswitch *tglswPtr = (Tglswitch *)recordPtr;
    double minVal = tglswPtr->tglsw.minVal;
    double maxVal = tglswPtr->tglsw.maxVal;
    double value;

    if (objc != 3) {
	Tcl_WrongNumArgs(interp, 1, objv, "set value");
	return TCL_ERROR;
    }

    if (Tcl_GetDoubleFromObj(interp, objv[2], &value) != TCL_OK) {
	return TCL_ERROR;
    }

    if (tglswPtr->core.state & TTK_STATE_DISABLED) {
	return TCL_OK;
    }

    /*
     * Limit new value to between minVal and maxVal
     */
    value = value < minVal ? minVal : value > maxVal ? maxVal : value;

    /*
     * Set value
     */
    Tcl_DecrRefCount(tglswPtr->tglsw.curValObj);
    tglswPtr->tglsw.curValObj = Tcl_NewDoubleObj(value);
    Tcl_IncrRefCount(tglswPtr->tglsw.curValObj);
    TtkRedisplayWidget(&tglswPtr->core);

    if (WidgetDestroyed(&tglswPtr->core)) {
	return TCL_ERROR;
    }

    return TCL_OK;
}

/*
 * $toggleswitch switchstate ?$boolean? --
 *	Modifies or inquires the widget's switch state.
 */
static int TglswitchSwitchstateCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Tglswitch *tglswPtr = (Tglswitch *)recordPtr;
    Ttk_State selState = (tglswPtr->core.state & TTK_STATE_SELECTED);
    Tcl_Obj *variableObj = tglswPtr->tglsw.variableObj;
    int arg2 = 0;

    if (objc == 2) {
	/*
	 * Return the widget's current switch state
	 */
	Tcl_SetObjResult(interp, Tcl_NewBooleanObj(selState));
    } else if (objc == 3) {
	if (Tcl_GetBooleanFromObj(interp, objv[2], &arg2) != TCL_OK) {
	    return TCL_ERROR;
	}

	if (tglswPtr->core.state & TTK_STATE_DISABLED) {
	    return TCL_OK;
	}

	/*
	 * Update the widget's selected state and current value
	 */
	Tcl_DecrRefCount(tglswPtr->tglsw.curValObj);
	if (arg2) {
	    TtkWidgetChangeState(&tglswPtr->core, TTK_STATE_SELECTED, 0);
	    tglswPtr->tglsw.curValObj = tglswPtr->tglsw.maxValObj;
	} else {
	    TtkWidgetChangeState(&tglswPtr->core, 0, TTK_STATE_SELECTED);
	    tglswPtr->tglsw.curValObj = tglswPtr->tglsw.minValObj;
	}
	Tcl_IncrRefCount(tglswPtr->tglsw.curValObj);

	if (!TkObjIsEmpty(variableObj)) {
	    /*
	     * Update the associated variable
	     */
	    Tcl_Obj *newOnOffValueObj = arg2 ? tglswPtr->tglsw.onValueObj
		    : tglswPtr->tglsw.offValueObj;
	    if (Tcl_ObjSetVar2(interp, variableObj, NULL, newOnOffValueObj,
		    TCL_GLOBAL_ONLY|TCL_LEAVE_ERR_MSG) == NULL) {
		return TCL_ERROR;
	    }
	}

	if (WidgetDestroyed(&tglswPtr->core)) {
	    return TCL_ERROR;
	}

	if ((tglswPtr->core.state & TTK_STATE_SELECTED) != selState) {
	    /*
	     * Evaluate the associated command at global scope
	     */
	    return Tcl_EvalObjEx(interp, tglswPtr->tglsw.commandObj,
		    TCL_EVAL_GLOBAL);
	}
    } else {
	Tcl_WrongNumArgs(interp, 1, objv, "switchstate ?boolean?");
	return TCL_ERROR;
    }

    return TCL_OK;
}

/*
 * $toggleswitch toggle --
 *	Toggles the widget's switch state.
 */
static int TglswitchToggleCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Tglswitch *tglswPtr = (Tglswitch *)recordPtr;
    static Tcl_Obj *newObjv[3];

    if (objc != 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "toggle");
	return TCL_ERROR;
    }

    newObjv[0] = objv[0];
    newObjv[1] = Tcl_NewStringObj("switchstate", -1);
    newObjv[2] = (tglswPtr->core.state & TTK_STATE_SELECTED) ?
		 Tcl_NewBooleanObj(0) : Tcl_NewBooleanObj(1);

    return TglswitchSwitchstateCommand(recordPtr, interp, 3, newObjv);
}

/*
 * $toggleswitch xcoord ?$value? --
 *	Returns the x coordinate corresponding to $value, or to the current
 *	value if $value is omitted.
 */
static int TglswitchXcoordCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Tglswitch *tglswPtr = (Tglswitch *)recordPtr;
    double value;
    int res = TCL_OK;

    if (objc == 3) {
	res = Tcl_GetDoubleFromObj(interp, objv[2], &value);
    } else if (objc == 2) {
	res = Tcl_GetDoubleFromObj(interp, tglswPtr->tglsw.curValObj, &value);
    } else {
	Tcl_WrongNumArgs(interp, 1, objv, "xcoord ?value?");
	return TCL_ERROR;
    }

    if (res == TCL_OK) {
	int x = ValueToPoint(tglswPtr, value);
	Tcl_SetObjResult(interp, Tcl_NewIntObj(x));
    }

    return res;
}

static const Ttk_Ensemble TglswitchCommands[] =
{
    { "cget",		TtkWidgetCgetCommand, 0 },
    { "configure",	TtkWidgetConfigureCommand, 0 },
    { "get",		TglswitchGetCommand, 0 },
    { "identify",	TtkWidgetIdentifyCommand, 0 },
    { "instate",	TtkWidgetInstateCommand, 0 },
    { "set",		TglswitchSetCommand, 0 },
    { "state",		TtkWidgetStateCommand, 0 },
    { "style",		TtkWidgetStyleCommand, 0 },
    { "switchstate",	TglswitchSwitchstateCommand, 0 },
    { "toggle",		TglswitchToggleCommand, 0 },
    { "xcoord",		TglswitchXcoordCommand, 0 },
    { 0, 0, 0 }
};

static const WidgetSpec TglswitchWidgetSpec =
{
    "Toggleswitch",		/* Class name */
    sizeof(Tglswitch),		/* record size */
    TglswitchOptionSpecs,	/* option specs */
    TglswitchCommands,		/* widget commands */
    TglswitchInitialize,	/* initialization proc */
    TglswitchCleanup,		/* cleanup proc */
    TglswitchConfigure,		/* configure proc */
    TglswitchPostConfigure,	/* postConfigure */
    TglswitchGetLayout,		/* getLayoutProc */
    TtkWidgetSize,		/* sizeProc */
    TglswitchDoLayout,		/* layoutProc */
    TtkWidgetDisplay		/* displayProc */
};

/*
 * Initialization.
 */
MODULE_SCOPE void TtkToggleswitch_Init(Tcl_Interp *interp)
{
    RegisterWidget(interp, "ttk::toggleswitch", &TglswitchWidgetSpec);
}
