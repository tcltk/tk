/*
 * Copyright © Joe English, Pat Thoyts, Michael Kirkham
 *
 * ttk::progressbar widget.
 */

#include "tkInt.h"
#include "ttkTheme.h"
#include "ttkWidget.h"

/*------------------------------------------------------------------------
 * +++ Widget record:
 */

#define DEF_PROGRESSBAR_LENGTH "100"
enum {
    TTK_PROGRESSBAR_DETERMINATE, TTK_PROGRESSBAR_INDETERMINATE
};
static const char *const ProgressbarModeStrings[] = {
    "determinate", "indeterminate", NULL
};

typedef struct {
    Tcl_Obj	*anchorObj;
    Tcl_Obj	*fontObj;
    Tcl_Obj	*foregroundObj;
    Tcl_Obj	*justifyObj;
    Tcl_Obj	*lengthObj;
    Tcl_Obj	*maximumObj;
    Tcl_Obj	*modeObj;
    Tcl_Obj	*orientObj;
    Tcl_Obj	*phaseObj;
    Tcl_Obj	*textObj;
    Tcl_Obj	*valueObj;
    Tcl_Obj	*variableObj;
    Tcl_Obj	*wrapLengthObj;

    int	mode;
    Ttk_TraceHandle *variableTrace;	/* Trace handle for -variable option */
    int	period;			/* Animation period */
    int	maxPhase;		/* Max animation phase */
    Tcl_TimerToken timer;		/* Animation timer */

} ProgressbarPart;

typedef struct {
    WidgetCore		core;
    ProgressbarPart	progress;
} Progressbar;

static const Tk_OptionSpec ProgressbarOptionSpecs[] =
{
    {TK_OPTION_ANCHOR, "-anchor", "anchor", "Anchor",
	"w", offsetof(Progressbar,progress.anchorObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK, 0, GEOMETRY_CHANGED},
    {TK_OPTION_FONT, "-font", "font", "Font",
	DEFAULT_FONT, offsetof(Progressbar,progress.fontObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK,0,GEOMETRY_CHANGED },
    {TK_OPTION_COLOR, "-foreground", "textColor", "TextColor",
	"black", offsetof(Progressbar,progress.foregroundObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK,0,0 },
    {TK_OPTION_JUSTIFY, "-justify", "justify", "Justify",
	"left", offsetof(Progressbar,progress.justifyObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK,0,GEOMETRY_CHANGED },
    {TK_OPTION_PIXELS, "-length", "length", "Length",
	DEF_PROGRESSBAR_LENGTH, offsetof(Progressbar,progress.lengthObj), TCL_INDEX_NONE,
	0, 0, GEOMETRY_CHANGED },
    {TK_OPTION_DOUBLE, "-maximum", "maximum", "Maximum",
	"100.0", offsetof(Progressbar,progress.maximumObj), TCL_INDEX_NONE,
	0, 0, 0 },
    {TK_OPTION_STRING_TABLE, "-mode", "mode", "ProgressMode", "determinate",
	offsetof(Progressbar,progress.modeObj),
	offsetof(Progressbar,progress.mode),
	0, ProgressbarModeStrings, 0 },
    {TK_OPTION_STRING_TABLE, "-orient", "orient", "Orient",
	"horizontal", offsetof(Progressbar,progress.orientObj), TCL_INDEX_NONE,
	0, ttkOrientStrings, STYLE_CHANGED },
    {TK_OPTION_INT, "-phase", "phase", "Phase",
	"0", offsetof(Progressbar,progress.phaseObj), TCL_INDEX_NONE,
	0, 0, 0 },
    {TK_OPTION_STRING, "-text", "text", "Text", "",
	offsetof(Progressbar,progress.textObj), TCL_INDEX_NONE,
	0,0,GEOMETRY_CHANGED },
    {TK_OPTION_DOUBLE, "-value", "value", "Value",
	"0.0", offsetof(Progressbar,progress.valueObj), TCL_INDEX_NONE,
	0, 0, 0 },
    {TK_OPTION_STRING, "-variable", "variable", "Variable",
	NULL, offsetof(Progressbar,progress.variableObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK, 0, 0 },
    {TK_OPTION_PIXELS, "-wraplength", "wrapLength", "WrapLength",
	"0", offsetof(Progressbar, progress.wrapLengthObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK,0,GEOMETRY_CHANGED},

    WIDGET_TAKEFOCUS_FALSE,
    WIDGET_INHERIT_OPTIONS(ttkCoreOptionSpecs)
};

/*------------------------------------------------------------------------
 * +++ Animation procedures:
 */

/* AnimationEnabled --
 *	Returns 1 if animation should be active, 0 otherwise.
 */
static int AnimationEnabled(Progressbar *pb)
{
    double maximum = 100, value = 0;

    Tcl_GetDoubleFromObj(NULL, pb->progress.maximumObj, &maximum);
    Tcl_GetDoubleFromObj(NULL, pb->progress.valueObj, &value);

    return (pb->progress.period > 0
	    && pb->progress.mode == TTK_PROGRESSBAR_INDETERMINATE);
}

/* AnimateProgressProc --
 *	Timer callback for progress bar animation.
 *	Increments the -phase option, redisplays the widget,
 *	and reschedules itself if animation still enabled.
 */
static void AnimateProgressProc(void *clientData)
{
    Progressbar *pb = (Progressbar *)clientData;

    pb->progress.timer = 0;
    if (AnimationEnabled(pb)) {
	int phase = 0;
	Tcl_GetIntFromObj(NULL, pb->progress.phaseObj, &phase);

	/*
	 * Update -phase:
	 */

	++phase;
	if (phase > pb->progress.maxPhase) {
	    phase = 0;
	}
	Tcl_DecrRefCount(pb->progress.phaseObj);
	pb->progress.phaseObj = Tcl_NewWideIntObj(phase);
	Tcl_IncrRefCount(pb->progress.phaseObj);

	/*
	 * Reschedule:
	 */

	pb->progress.timer = Tcl_CreateTimerHandler(
	    pb->progress.period, AnimateProgressProc, clientData);
	TtkRedisplayWidget(&pb->core);
    }
}

/* CheckAnimation --
 *	If animation is enabled and not scheduled, schedule it.
 *	If animation is disabled but scheduled, cancel it.
 */
static void CheckAnimation(Progressbar *pb)
{
    if (AnimationEnabled(pb)) {
	if (pb->progress.timer == 0) {
	    pb->progress.timer = Tcl_CreateTimerHandler(
		pb->progress.period, AnimateProgressProc, pb);
	}
    } else {
	if (pb->progress.timer != 0) {
	    Tcl_DeleteTimerHandler(pb->progress.timer);
	    pb->progress.timer = 0;
	}
    }
}

/*------------------------------------------------------------------------
 * +++ Trace hook for progressbar -variable option:
 */

static void VariableChanged(void *recordPtr, const char *value)
{
    Progressbar *pb = (Progressbar *)recordPtr;
    Tcl_Obj *newValue;
    double scratch;

    if (WidgetDestroyed(&pb->core)) {
	return;
    }

    if (!value) {
	/* Linked variable is unset -- disable widget */
	TtkWidgetChangeState(&pb->core, TTK_STATE_DISABLED, 0);
	return;
    }
    TtkWidgetChangeState(&pb->core, 0, TTK_STATE_DISABLED);

    newValue = Tcl_NewStringObj(value, -1);
    Tcl_IncrRefCount(newValue);
    if (Tcl_GetDoubleFromObj(NULL, newValue, &scratch) != TCL_OK) {
	TtkWidgetChangeState(&pb->core, TTK_STATE_INVALID, 0);
	return;
    }
    TtkWidgetChangeState(&pb->core, 0, TTK_STATE_INVALID);
    Tcl_DecrRefCount(pb->progress.valueObj);
    pb->progress.valueObj = newValue;

    CheckAnimation(pb);
    TtkRedisplayWidget(&pb->core);
}

/*------------------------------------------------------------------------
 * +++ Widget class methods:
 */

static void ProgressbarInitialize(
    TCL_UNUSED(Tcl_Interp *),
    void *recordPtr)
{
    Progressbar *pb = (Progressbar *)recordPtr;

    pb->progress.variableTrace = 0;
    pb->progress.timer = 0;
}

static void ProgressbarCleanup(void *recordPtr)
{
    Progressbar *pb = (Progressbar *)recordPtr;
    if (pb->progress.variableTrace)
	Ttk_UntraceVariable(pb->progress.variableTrace);
    if (pb->progress.timer)
	Tcl_DeleteTimerHandler(pb->progress.timer);
}

/*
 * Configure hook:
 *
 * @@@ TODO: deal with [$pb configure -value ... -variable ...]
 */
static int ProgressbarConfigure(Tcl_Interp *interp, void *recordPtr, int mask)
{
    Progressbar *pb = (Progressbar *)recordPtr;
    Tcl_Obj *varName = pb->progress.variableObj;
    Ttk_TraceHandle *vt = 0;

    if (varName != NULL && *Tcl_GetString(varName) != '\0') {
	vt = Ttk_TraceVariable(interp, varName, VariableChanged, recordPtr);
	if (!vt) return TCL_ERROR;
    }

    if (TtkCoreConfigure(interp, recordPtr, mask) != TCL_OK) {
	if (vt) Ttk_UntraceVariable(vt);
	return TCL_ERROR;
    }

    if (pb->progress.variableTrace) {
	Ttk_UntraceVariable(pb->progress.variableTrace);
    }
    pb->progress.variableTrace = vt;

    return TCL_OK;
}

/*
 * Post-configuration hook:
 */
static int ProgressbarPostConfigure(
    TCL_UNUSED(Tcl_Interp *),
    void *recordPtr,
    TCL_UNUSED(int))
{
    Progressbar *pb = (Progressbar *)recordPtr;
    int status = TCL_OK;

    if (pb->progress.variableTrace) {
	status = Ttk_FireTrace(pb->progress.variableTrace);
	if (WidgetDestroyed(&pb->core)) {
	    return TCL_ERROR;
	}
	if (status != TCL_OK) {
	    /* Unset -variable: */
	    Ttk_UntraceVariable(pb->progress.variableTrace);
	    Tcl_DecrRefCount(pb->progress.variableObj);
	    pb->progress.variableTrace = 0;
	    pb->progress.variableObj = NULL;
	    return TCL_ERROR;
	}
    }

    CheckAnimation(pb);

    return status;
}

/*
 * Size hook:
 *	Compute base layout size, overrid
 */
static int ProgressbarSize(void *recordPtr, int *widthPtr, int *heightPtr)
{
    Progressbar *pb = (Progressbar *)recordPtr;
    int length = 100;
    Ttk_Orient orient = TTK_ORIENT_HORIZONTAL;

    TtkWidgetSize(recordPtr, widthPtr, heightPtr);

    /* Override requested width (height) based on -length and -orient
     */
    Tk_GetPixelsFromObj(NULL, pb->core.tkwin, pb->progress.lengthObj, &length);
    Ttk_GetOrientFromObj(NULL, pb->progress.orientObj, &orient);

    if (orient == TTK_ORIENT_HORIZONTAL) {
	*widthPtr = length;
    } else {
	*heightPtr = length;
    }

    return 1;
}

/*
 * Layout hook:
 *	Adjust size and position of pbar element, if present.
 */

static void ProgressbarDeterminateLayout(
    Progressbar *pb,
    Ttk_Element pbar,
    Ttk_Box parcel,
    double fraction,
    Ttk_Orient orient)
{
    if (fraction < 0.0) fraction = 0.0;
    if (fraction > 1.0) fraction = 1.0;

    if (orient == TTK_ORIENT_HORIZONTAL) {
	parcel.width = (int)(parcel.width * fraction);
    } else {
	int newHeight = (int)(parcel.height * fraction);
	parcel.y += (parcel.height - newHeight);
	parcel.height = newHeight;
    }
    Ttk_PlaceElement(pb->core.layout, pbar, parcel);
}

static void ProgressbarIndeterminateLayout(
    Progressbar *pb,
    Ttk_Element pbar,
    Ttk_Box parcel,
    double fraction,
    Ttk_Orient orient)
{
    Ttk_Box pbarBox = Ttk_ElementParcel(pbar);

    fraction = fmod(fabs(fraction), 2.0);
    if (fraction > 1.0) {
	fraction = 2.0 - fraction;
    }

    if (orient == TTK_ORIENT_HORIZONTAL) {
	pbarBox.x = parcel.x + (int)(fraction * (parcel.width-pbarBox.width));
    } else {
	pbarBox.y = parcel.y + (int)(fraction * (parcel.height-pbarBox.height));
    }
    Ttk_PlaceElement(pb->core.layout, pbar, pbarBox);
}

static void ProgressbarDoLayout(void *recordPtr)
{
    Progressbar *pb = (Progressbar *)recordPtr;
    WidgetCore *corePtr = &pb->core;
    Ttk_Element pbar = Ttk_FindElement(corePtr->layout, "pbar");
    double value = 0.0, maximum = 100.0;
    Ttk_Orient orient = TTK_ORIENT_HORIZONTAL;

    Ttk_PlaceLayout(corePtr->layout,corePtr->state,Ttk_WinBox(corePtr->tkwin));

    /* Adjust the bar size:
     */

    Tcl_GetDoubleFromObj(NULL, pb->progress.valueObj, &value);
    Tcl_GetDoubleFromObj(NULL, pb->progress.maximumObj, &maximum);
    Ttk_GetOrientFromObj(NULL, pb->progress.orientObj, &orient);

    if (pbar) {
	double fraction = value / maximum;
	Ttk_Box parcel = Ttk_ClientRegion(corePtr->layout, "trough");

	if (pb->progress.mode == TTK_PROGRESSBAR_DETERMINATE) {
	    ProgressbarDeterminateLayout(
		pb, pbar, parcel, fraction, orient);
	} else {
	    ProgressbarIndeterminateLayout(
		pb, pbar, parcel, fraction, orient);
	}
    }
}

static Ttk_Layout ProgressbarGetLayout(
    Tcl_Interp *interp, Ttk_Theme theme, void *recordPtr)
{
    Progressbar *pb = (Progressbar *)recordPtr;
    Ttk_Layout layout = TtkWidgetGetOrientedLayout(
	interp, theme, recordPtr, pb->progress.orientObj);

    /*
     * Check if the style supports animation:
     */
    pb->progress.period = 0;
    pb->progress.maxPhase = 0;
    if (layout) {
	Tcl_Obj *periodObj = Ttk_QueryOption(layout, "-period", 0);
	Tcl_Obj *maxPhaseObj = Ttk_QueryOption(layout, "-maxphase", 0);
	if (periodObj)
	    Tcl_GetIntFromObj(NULL, periodObj, &pb->progress.period);
	if (maxPhaseObj)
	    Tcl_GetIntFromObj(NULL, maxPhaseObj, &pb->progress.maxPhase);
    }

    return layout;
}

/*------------------------------------------------------------------------
 * +++ Widget commands:
 */

/* $sb step ?amount?
 */
static int ProgressbarStepCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Progressbar *pb = (Progressbar *)recordPtr;
    double value = 0.0, stepAmount = 1.0;
    Tcl_Obj *newValueObj;

    if (objc == 3) {
	if (Tcl_GetDoubleFromObj(interp, objv[2], &stepAmount) != TCL_OK) {
	    return TCL_ERROR;
	}
    } else if (objc != 2) {
	Tcl_WrongNumArgs(interp, 2,objv, "?stepAmount?");
	return TCL_ERROR;
    }

    (void)Tcl_GetDoubleFromObj(NULL, pb->progress.valueObj, &value);
    value += stepAmount;

    /* In determinate mode, wrap around if value exceeds maximum:
     */
    if (pb->progress.mode == TTK_PROGRESSBAR_DETERMINATE) {
	double maximum = 100.0;
	(void)Tcl_GetDoubleFromObj(NULL, pb->progress.maximumObj, &maximum);
	value = fmod(value, maximum);
    }

    newValueObj = Tcl_NewDoubleObj(value);
    Tcl_IncrRefCount(newValueObj);

    TtkRedisplayWidget(&pb->core);

    /* Update value by setting the linked -variable, if there is one:
     */
    if (pb->progress.variableTrace) {
	int result = Tcl_ObjSetVar2(
			interp, pb->progress.variableObj, 0, newValueObj,
			TCL_GLOBAL_ONLY | TCL_LEAVE_ERR_MSG)
		? TCL_OK : TCL_ERROR;
	Tcl_DecrRefCount(newValueObj);
	return result;
    }

    /* Otherwise, change the -value directly:
     */
    Tcl_DecrRefCount(pb->progress.valueObj);
    pb->progress.valueObj = newValueObj;
    CheckAnimation(pb);

    return TCL_OK;
}

/* $sb start|stop ?args? --
 * Change [$sb $cmd ...] to [ttk::progressbar::$cmd ...]
 * and pass to interpreter.
 */
static int ProgressbarStartStopCommand(
    Tcl_Interp *interp, const char *cmdName, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Tcl_Obj *cmd = Tcl_NewListObj(objc, objv);
    Tcl_Obj *prefix[2];
    int status;

    /* ASSERT: objc >= 2 */

    prefix[0] = Tcl_NewStringObj(cmdName, -1);
    prefix[1] = objv[0];
    Tcl_ListObjReplace(interp, cmd, 0, 2, 2,prefix);

    Tcl_IncrRefCount(cmd);
    status = Tcl_EvalObjEx(interp, cmd, 0);
    Tcl_DecrRefCount(cmd);

    return status;
}

static int ProgressbarStartCommand(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    Tcl_Size objc,
    Tcl_Obj *const objv[])
{
    return ProgressbarStartStopCommand(
	    interp, "::ttk::progressbar::start", objc, objv);
}

static int ProgressbarStopCommand(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,
    Tcl_Size objc,
    Tcl_Obj *const objv[])
{
    return ProgressbarStartStopCommand(
	    interp, "::ttk::progressbar::stop", objc, objv);
}

static const Ttk_Ensemble ProgressbarCommands[] = {
    { "cget",		TtkWidgetCgetCommand,0 },
    { "configure",	TtkWidgetConfigureCommand,0 },
    { "identify",	TtkWidgetIdentifyCommand,0 },
    { "instate",	TtkWidgetInstateCommand,0 },
    { "start",		ProgressbarStartCommand,0 },
    { "state",	TtkWidgetStateCommand,0 },
    { "step",		ProgressbarStepCommand,0 },
    { "stop",		ProgressbarStopCommand,0 },
    { "style",		TtkWidgetStyleCommand,0 },
    { 0,0,0 }
};

/*
 * Widget specification:
 */
static const WidgetSpec ProgressbarWidgetSpec =
{
    "TProgressbar",		/* className */
    sizeof(Progressbar),	/* recordSize */
    ProgressbarOptionSpecs,	/* optionSpecs */
    ProgressbarCommands,	/* subcommands */
    ProgressbarInitialize,	/* initializeProc */
    ProgressbarCleanup,		/* cleanupProc */
    ProgressbarConfigure,	/* configureProc */
    ProgressbarPostConfigure,	/* postConfigureProc */
    ProgressbarGetLayout,	/* getLayoutProc */
    ProgressbarSize,		/* sizeProc */
    ProgressbarDoLayout,	/* layoutProc */
    TtkWidgetDisplay		/* displayProc */
};

/*
 * Layouts:
 */
TTK_BEGIN_LAYOUT(VerticalProgressbarLayout)
    TTK_GROUP("Vertical.Progressbar.trough", TTK_FILL_BOTH,
	TTK_NODE("Vertical.Progressbar.pbar", TTK_PACK_BOTTOM|TTK_FILL_X))
TTK_END_LAYOUT

TTK_BEGIN_LAYOUT(HorizontalProgressbarLayout)
    TTK_GROUP("Horizontal.Progressbar.trough", TTK_FILL_BOTH,
	TTK_NODE("Horizontal.Progressbar.pbar", TTK_PACK_LEFT|TTK_FILL_Y)
	TTK_NODE("Horizontal.Progressbar.ctext", TTK_PACK_LEFT))
TTK_END_LAYOUT

/*
 * Initialization:
 */

MODULE_SCOPE void
TtkProgressbar_Init(Tcl_Interp *interp)
{
    Ttk_Theme themePtr = Ttk_GetDefaultTheme(interp);

    Ttk_RegisterLayout(themePtr,
	"Vertical.TProgressbar", VerticalProgressbarLayout);
    Ttk_RegisterLayout(themePtr,
	"Horizontal.TProgressbar", HorizontalProgressbarLayout);

    RegisterWidget(interp, "ttk::progressbar", &ProgressbarWidgetSpec);
}

/*EOF*/
