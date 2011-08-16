/*
 * Copyright (C) 2011 Goodwin Lawlor goodwin.lawlor@gmail.com
 *
 * ttk::range widget.
 */

#include <tk.h>
#include <string.h>
#include <stdio.h>
#include "ttkTheme.h"
#include "ttkWidget.h"

#define DEF_RANGE_LENGTH "100"

#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define MIN(a,b) ((a) < (b) ? (a) : (b))

/*
 * Range widget record
 */
typedef struct
{
    /* slider element options */
    Tcl_Obj *fromObj;         /* from value - lower bound*/
    Tcl_Obj *toObj;           /* to value - upper bound*/
    Tcl_Obj *minvalueObj;          /* min value */
    Tcl_Obj *maxvalueObj;          /* max value */
    Tcl_Obj *lengthObj;       /* length of the long axis of the range */
    Tcl_Obj *orientObj;       /* widget orientation */
    int orient;

    /* widget options */
    Tcl_Obj *commandObj;
    Tcl_Obj *minvariableObj;
    Tcl_Obj *maxvariableObj;

    /* internal state */
    Ttk_TraceHandle *minvariableTrace;
    Ttk_TraceHandle *maxvariableTrace;

} RangePart;

typedef struct
{
    WidgetCore core;
    RangePart  range;
} Range;

static Tk_OptionSpec RangeOptionSpecs[] =
{
    WIDGET_TAKES_FOCUS,

    {TK_OPTION_STRING, "-command", "command", "Command", "",
	Tk_Offset(Range,range.commandObj), -1,
	TK_OPTION_NULL_OK,0,0},
    {TK_OPTION_STRING, "-minvariable", "minvariable", "Minvariable", "",
	Tk_Offset(Range,range.minvariableObj), -1,
	0,0,0},
    {TK_OPTION_STRING, "-maxvariable", "maxvariable", "Maxvariable", "",
	Tk_Offset(Range,range.maxvariableObj), -1,
	0,0,0},
    {TK_OPTION_STRING_TABLE, "-orient", "orient", "Orient", "horizontal",
	Tk_Offset(Range,range.orientObj),
	Tk_Offset(Range,range.orient), 0,
	(ClientData)ttkOrientStrings, STYLE_CHANGED },

    {TK_OPTION_DOUBLE, "-from", "from", "From", "0",
	Tk_Offset(Range,range.fromObj), -1, 0, 0, 0},
    {TK_OPTION_DOUBLE, "-to", "to", "To", "1.0",
	Tk_Offset(Range,range.toObj), -1, 0, 0, 0},
    {TK_OPTION_DOUBLE, "-minvalue", "minvalue", "Minvalue", "0",
	Tk_Offset(Range,range.minvalueObj), -1, 0, 0, 0},
    {TK_OPTION_DOUBLE, "-maxvalue", "maxvalue", "Maxvalue", "1.0",
	Tk_Offset(Range,range.maxvalueObj), -1, 0, 0, 0},
    {TK_OPTION_PIXELS, "-length", "length", "Length",
	DEF_RANGE_LENGTH, Tk_Offset(Range,range.lengthObj), -1, 0, 0,
    	GEOMETRY_CHANGED},

    WIDGET_INHERIT_OPTIONS(ttkCoreOptionSpecs)
};

static XPoint ValueToPoint(Range *rangePtr, double value);
static double PointToValue(Range *rangePtr, int x, int y);

/* RangeMinVariableChanged --
 * 	Variable trace procedure for range -variable;
 * 	Updates the range's value.
 * 	If the linked variable is not a valid double,
 * 	sets the 'invalid' state.
 */
static void RangeMinVariableChanged(void *recordPtr, const char *minvalue)
{
    Range *range = recordPtr;
    double v;

    if (minvalue == NULL || Tcl_GetDouble(0, minvalue, &v) != TCL_OK) {
	TtkWidgetChangeState(&range->core, TTK_STATE_INVALID, 0);
    } else {
	Tcl_Obj *minvalueObj = Tcl_NewDoubleObj(v);
	Tcl_IncrRefCount(minvalueObj);
	Tcl_DecrRefCount(range->range.minvalueObj);
	range->range.minvalueObj = minvalueObj;
	TtkWidgetChangeState(&range->core, 0, TTK_STATE_INVALID);
    }
    TtkRedisplayWidget(&range->core);
}

/* RangeMaxVariableChanged --
 * 	Variable trace procedure for range -variable;
 * 	Updates the range's value.
 * 	If the linked variable is not a valid double,
 * 	sets the 'invalid' state.
 */
static void RangeMaxVariableChanged(void *recordPtr, const char *maxvalue)
{
    Range *range = recordPtr;
    double v;

    if (maxvalue == NULL || Tcl_GetDouble(0, maxvalue, &v) != TCL_OK) {
	TtkWidgetChangeState(&range->core, TTK_STATE_INVALID, 0);
    } else {
	Tcl_Obj *maxvalueObj = Tcl_NewDoubleObj(v);
	Tcl_IncrRefCount(maxvalueObj);
	Tcl_DecrRefCount(range->range.maxvalueObj);
	range->range.maxvalueObj = maxvalueObj;
	TtkWidgetChangeState(&range->core, 0, TTK_STATE_INVALID);
    }
    TtkRedisplayWidget(&range->core);
}

/* RangeInitialize --
 * 	Range widget initialization hook.
 */
static void RangeInitialize(Tcl_Interp *interp, void *recordPtr)
{
    Range *rangePtr = recordPtr;
    TtkTrackElementState(&rangePtr->core);

}

static void RangeCleanup(void *recordPtr)
{
    Range *range = recordPtr;

    if (range->range.minvariableTrace) {
	Ttk_UntraceVariable(range->range.minvariableTrace);
	range->range.minvariableTrace = 0;
    }

    if (range->range.maxvariableTrace) {
	Ttk_UntraceVariable(range->range.maxvariableTrace);
	range->range.maxvariableTrace = 0;
    }
}

/* RangeConfigure --
 * 	Configuration hook.
 */
static int RangeConfigure(Tcl_Interp *interp, void *recordPtr, int mask)
{
    Range *range = recordPtr;
    Tcl_Obj *minvarName = range->range.minvariableObj;
    Tcl_Obj *maxvarName = range->range.maxvariableObj;
    Ttk_TraceHandle *minvt = 0;
    Ttk_TraceHandle *maxvt = 0;

    if (minvarName != NULL && *Tcl_GetString(minvarName) != '\0') {
	minvt = Ttk_TraceVariable(interp,minvarName, RangeMinVariableChanged,recordPtr);
	if (!minvt) return TCL_ERROR;
    }

    if (maxvarName != NULL && *Tcl_GetString(maxvarName) != '\0') {
	maxvt = Ttk_TraceVariable(interp,maxvarName, RangeMaxVariableChanged,recordPtr);
	if (!maxvt) return TCL_ERROR;
    }

    if (TtkCoreConfigure(interp, recordPtr, mask) != TCL_OK) {
	if (minvt) Ttk_UntraceVariable(minvt);
	if (maxvt) Ttk_UntraceVariable(maxvt);
	return TCL_ERROR;
    }

    if (range->range.minvariableTrace) {
	Ttk_UntraceVariable(range->range.minvariableTrace);
    }
    range->range.minvariableTrace = minvt;

    if (range->range.maxvariableTrace) {
	Ttk_UntraceVariable(range->range.maxvariableTrace);
    }
    range->range.maxvariableTrace = maxvt;

    return TCL_OK;
}

/* RangePostConfigure --
 * 	Post-configuration hook.
 */
static int RangePostConfigure(
    Tcl_Interp *interp, void *recordPtr, int mask)
{
    Range *range = recordPtr;
    int minstatus = TCL_OK;
    int maxstatus = TCL_OK;

    if (range->range.minvariableTrace) {
	minstatus = Ttk_FireTrace(range->range.minvariableTrace);
	if (WidgetDestroyed(&range->core)) {
	    return TCL_ERROR;
	}
	if (minstatus != TCL_OK) {
	    /* Unset -variable: */
	    Ttk_UntraceVariable(range->range.minvariableTrace);
	    Tcl_DecrRefCount(range->range.minvariableObj);
	    range->range.minvariableTrace = 0;
	    range->range.minvariableObj = NULL;
	    minstatus = TCL_ERROR;
	}
    }

    if (range->range.maxvariableTrace) {
	maxstatus = Ttk_FireTrace(range->range.maxvariableTrace);
	if (WidgetDestroyed(&range->core)) {
	    return TCL_ERROR;
	}
	if (maxstatus != TCL_OK) {
	    /* Unset -variable: */
	    Ttk_UntraceVariable(range->range.maxvariableTrace);
	    Tcl_DecrRefCount(range->range.maxvariableObj);
	    range->range.maxvariableTrace = 0;
	    range->range.maxvariableObj = NULL;
	    maxstatus = TCL_ERROR;
	}
    }
    if (minstatus != TCL_OK || maxstatus != TCL_OK) {
	return TCL_ERROR;
    } else {
	return TCL_OK;
    }

}

/* RangeGetLayout --
 *	getLayout hook.
 */
static Ttk_Layout
RangeGetLayout(Tcl_Interp *interp, Ttk_Theme theme, void *recordPtr)
{
    Range *rangePtr = recordPtr;
    return TtkWidgetGetOrientedLayout(
	interp, theme, recordPtr, rangePtr->range.orientObj);
}

/*
 * TroughBox --
 * 	Returns the inner area of the trough element.
 */
static Ttk_Box TroughBox(Range *rangePtr)
{
    return Ttk_ClientRegion(rangePtr->core.layout, "trough");
}

/*
 * TroughRange --
 * 	Return the value area of the trough element, adjusted
 * 	for slider size.
 */
static Ttk_Box TroughRange(Range *rangePtr)
{
    Ttk_Box troughBox = TroughBox(rangePtr);
    Ttk_Element slider = Ttk_FindElement(rangePtr->core.layout,"minslider");

    /*
     * If this is a range widget, adjust range for slider:
     */
    if (slider) {
	Ttk_Box sliderBox = Ttk_ElementParcel(slider);
	if (rangePtr->range.orient == TTK_ORIENT_HORIZONTAL) {
	    troughBox.x += sliderBox.width / 2;
	    troughBox.width -= sliderBox.width;
	} else {
	    troughBox.y += sliderBox.height / 2;
	    troughBox.height -= sliderBox.height;
	}
    }

    return troughBox;
}

/*
 * RangeFraction --
 */
static double RangeFraction(Range *rangePtr, double value)
{
    double from = 0, to = 1, fraction;

    Tcl_GetDoubleFromObj(NULL, rangePtr->range.fromObj, &from);
    Tcl_GetDoubleFromObj(NULL, rangePtr->range.toObj, &to);

    if (from == to) {
	return 1.0;
    }

    fraction = (value - from) / (to - from);

    return fraction < 0 ? 0 : fraction > 1 ? 1 : fraction;
}

/* $range get ?x y? --
 * 	Returns the current value of the range widget, or if $x and
 * 	$y are specified, the value represented by point @x,y.
 */
static int
RangeGetCommand(
    void *recordPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    Range *rangePtr = recordPtr;
    int x, y, r = TCL_OK;
    double value = 0;

    if (objc != 4) {
	Tcl_WrongNumArgs(interp, 1, objv, "get ?x y?");
	return TCL_ERROR;
    }

    r = Tcl_GetIntFromObj(interp, objv[2], &x);
    if (r == TCL_OK)
	r = Tcl_GetIntFromObj(interp, objv[3], &y);
    if (r == TCL_OK) {
	value = PointToValue(rangePtr, x, y);
	Tcl_SetObjResult(interp, Tcl_NewDoubleObj(value));
    }

    return r;
}

static int
RangeGetMinCommand(
    void *recordPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    Range *rangePtr = recordPtr;

    if (objc != 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "getmin");
	return TCL_ERROR;
    }

    Tcl_SetObjResult(interp, rangePtr->range.minvalueObj);

    return TCL_OK;
}

static int
RangeGetMaxCommand(
    void *recordPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    Range *rangePtr = recordPtr;

    if (objc != 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "getmax");
	return TCL_ERROR;
    }

    Tcl_SetObjResult(interp, rangePtr->range.maxvalueObj);

    return TCL_OK;
}

/* $range setmin $newValue
 */
static int
RangeSetMinCommand(
    void *recordPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    Range *rangePtr = recordPtr;
    double from = 0.0, to = 1.0, minvalue, maxvalue;
    int result = TCL_OK;

    if (objc != 3) {
	Tcl_WrongNumArgs(interp, 1, objv, "setmin value");
	return TCL_ERROR;
    }

    if (Tcl_GetDoubleFromObj(interp, objv[2], &minvalue) != TCL_OK) {
	return TCL_ERROR;
    }

    if (rangePtr->core.state & TTK_STATE_DISABLED) {
	return TCL_OK;
    }

    /* ASSERT: fromObj and toObj are valid doubles.
     */
    Tcl_GetDoubleFromObj(interp, rangePtr->range.fromObj, &from);
    Tcl_GetDoubleFromObj(interp, rangePtr->range.toObj, &to);

    /* Limit new value to between 'from' and 'to':
     */
    if (from < to) {
	minvalue = minvalue < from ? from : minvalue > to ? to : minvalue;
    } else {
	minvalue = minvalue < to ? to : minvalue > from ? from : minvalue;
    }

    // Must be <= maxvalue too.
    Tcl_GetDoubleFromObj(NULL, rangePtr->range.maxvalueObj, &maxvalue);
    minvalue = minvalue < maxvalue ? minvalue : maxvalue;

    /*
     * Set value:
     */
    Tcl_DecrRefCount(rangePtr->range.minvalueObj);
    rangePtr->range.minvalueObj = Tcl_NewDoubleObj(minvalue);
    Tcl_IncrRefCount(rangePtr->range.minvalueObj);
    TtkRedisplayWidget(&rangePtr->core);

    /*
     * Set attached variable, if any:
     */
    if (rangePtr->range.minvariableObj != NULL) {
	Tcl_ObjSetVar2(interp, rangePtr->range.minvariableObj, NULL,
	    rangePtr->range.minvalueObj, TCL_GLOBAL_ONLY);
    }
    if (WidgetDestroyed(&rangePtr->core)) {
	return TCL_ERROR;
    }

    /*
     * Invoke -command, if any:
     */
    if (rangePtr->range.commandObj != NULL) {
	Tcl_Obj *cmdObj = Tcl_DuplicateObj(rangePtr->range.commandObj);
	Tcl_IncrRefCount(cmdObj);
	Tcl_AppendToObj(cmdObj, " ", 1);
	Tcl_AppendObjToObj(cmdObj, rangePtr->range.minvalueObj);
	Tcl_AppendToObj(cmdObj, " ", 1);
	Tcl_AppendObjToObj(cmdObj, rangePtr->range.maxvalueObj);
	result = Tcl_EvalObjEx(interp, cmdObj, TCL_EVAL_GLOBAL);
	Tcl_DecrRefCount(cmdObj);
    }

    return result;
}

/* $range setmax $newValue
 */
static int
RangeSetMaxCommand(
    void *recordPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    Range *rangePtr = recordPtr;
    double from = 0.0, to = 1.0, minvalue, maxvalue;
    int result = TCL_OK;

    if (objc != 3) {
	Tcl_WrongNumArgs(interp, 1, objv, "setmax value");
	return TCL_ERROR;
    }

    if (Tcl_GetDoubleFromObj(interp, objv[2], &maxvalue) != TCL_OK) {
	return TCL_ERROR;
    }

    if (rangePtr->core.state & TTK_STATE_DISABLED) {
	return TCL_OK;
    }

    /* ASSERT: fromObj and toObj are valid doubles.
     */
    Tcl_GetDoubleFromObj(interp, rangePtr->range.fromObj, &from);
    Tcl_GetDoubleFromObj(interp, rangePtr->range.toObj, &to);

    /* Limit new value to between 'from' and 'to':
     */
    if (from < to) {
	maxvalue = maxvalue < from ? from : maxvalue > to ? to : maxvalue;
    } else {
	maxvalue = maxvalue < to ? to : maxvalue > from ? from : maxvalue;
    }

    // Must be >= minvalue too.
    Tcl_GetDoubleFromObj(NULL, rangePtr->range.minvalueObj, &minvalue);
    maxvalue = maxvalue > minvalue ? maxvalue : minvalue;

    /*
     * Set value:
     */
    Tcl_DecrRefCount(rangePtr->range.maxvalueObj);
    rangePtr->range.maxvalueObj = Tcl_NewDoubleObj(maxvalue);
    Tcl_IncrRefCount(rangePtr->range.maxvalueObj);
    TtkRedisplayWidget(&rangePtr->core);

    /*
     * Set attached variable, if any:
     */
    if (rangePtr->range.maxvariableObj != NULL) {
	Tcl_ObjSetVar2(interp, rangePtr->range.maxvariableObj, NULL,
	    rangePtr->range.maxvalueObj, TCL_GLOBAL_ONLY);
    }
    if (WidgetDestroyed(&rangePtr->core)) {
	return TCL_ERROR;
    }

    /*
     * Invoke -command, if any:
     */
    if (rangePtr->range.commandObj != NULL) {
	Tcl_Obj *cmdObj = Tcl_DuplicateObj(rangePtr->range.commandObj);
	Tcl_IncrRefCount(cmdObj);
	Tcl_AppendToObj(cmdObj, " ", 1);
	Tcl_AppendObjToObj(cmdObj, rangePtr->range.minvalueObj);
	Tcl_AppendToObj(cmdObj, " ", 1);
	Tcl_AppendObjToObj(cmdObj, rangePtr->range.maxvalueObj);
	result = Tcl_EvalObjEx(interp, cmdObj, TCL_EVAL_GLOBAL);
	Tcl_DecrRefCount(cmdObj);
    }

    return result;
}

static int
RangeCoordsCommand(
    void *recordPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    Range *rangePtr = recordPtr;
    double value;
    int r = TCL_OK;

    if (objc != 3) {
	Tcl_WrongNumArgs(interp, 1, objv, "coords ?value?");
	return TCL_ERROR;
    }

    r = Tcl_GetDoubleFromObj(interp, objv[2], &value);

    if (r == TCL_OK) {
	Tcl_Obj *point[2];
	XPoint pt = ValueToPoint(rangePtr, value);
	point[0] = Tcl_NewIntObj(pt.x);
	point[1] = Tcl_NewIntObj(pt.y);
	Tcl_SetObjResult(interp, Tcl_NewListObj(2, point));
    }
    return r;
}

static int
RangeMinCoordsCommand(
    void *recordPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    Range *rangePtr = recordPtr;
    double value;
    int r = TCL_OK;

    if (objc != 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "mincoords");
	return TCL_ERROR;
    }

    r = Tcl_GetDoubleFromObj(interp, rangePtr->range.minvalueObj, &value);

    if (r == TCL_OK) {
	Tcl_Obj *point[2];
	XPoint pt = ValueToPoint(rangePtr, value);
	point[0] = Tcl_NewIntObj(pt.x);
	point[1] = Tcl_NewIntObj(pt.y);
	Tcl_SetObjResult(interp, Tcl_NewListObj(2, point));
    }
    return r;
}

static int
RangeMaxCoordsCommand(
    void *recordPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
    Range *rangePtr = recordPtr;
    double value;
    int r = TCL_OK;

    if (objc != 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "maxcoords");
	return TCL_ERROR;
    }

    r = Tcl_GetDoubleFromObj(interp, rangePtr->range.maxvalueObj, &value);

    if (r == TCL_OK) {
	Tcl_Obj *point[2];
	XPoint pt = ValueToPoint(rangePtr, value);
	point[0] = Tcl_NewIntObj(pt.x);
	point[1] = Tcl_NewIntObj(pt.y);
	Tcl_SetObjResult(interp, Tcl_NewListObj(2, point));
    }
    return r;
}

static void RangeDoLayout(void *clientData)
{
    WidgetCore *corePtr = clientData;
    Ttk_Element minslider = Ttk_FindElement(corePtr->layout, "minslider");
    Ttk_Element maxslider = Ttk_FindElement(corePtr->layout, "maxslider");

    Ttk_PlaceLayout(corePtr->layout,corePtr->state,Ttk_WinBox(corePtr->tkwin));

    /* Adjust the slider position:
     */
    if (minslider && maxslider) {
	Range *rangePtr = clientData;
	Ttk_Box troughBox = TroughBox(rangePtr);
	Ttk_Box minsliderBox = Ttk_ElementParcel(minslider);
	Ttk_Box maxsliderBox = Ttk_ElementParcel(maxslider);
	double minvalue = 0.0, maxvalue = 1.0;
	double minfraction, maxfraction;
	int range, offset;

	Tcl_GetDoubleFromObj(NULL, rangePtr->range.minvalueObj, &minvalue);
	Tcl_GetDoubleFromObj(NULL, rangePtr->range.maxvalueObj, &maxvalue);
	minfraction = RangeFraction(rangePtr, minvalue);
	maxfraction = RangeFraction(rangePtr, maxvalue);

	if (rangePtr->range.orient == TTK_ORIENT_HORIZONTAL) {
	    range = troughBox.width - minsliderBox.width;
	    offset = minsliderBox.x;
	    minsliderBox.x = offset + (int)(minfraction * range);
	    maxsliderBox.x = offset + (int)(maxfraction * range);
	} else {
	    range = troughBox.height - minsliderBox.height;
	    offset = minsliderBox.y;
	    minsliderBox.y = offset + (int)(minfraction * range);
	    maxsliderBox.y = offset + (int)(maxfraction * range);
	}

	Ttk_PlaceElement(corePtr->layout, minslider, minsliderBox);
	Ttk_PlaceElement(corePtr->layout, maxslider, maxsliderBox);

    }
}

/*
 * RangeSize --
 * 	Compute requested size of range.
 */
static int RangeSize(void *clientData, int *widthPtr, int *heightPtr)
{
    WidgetCore *corePtr = clientData;
    Range *rangePtr = clientData;
    int length;

    Ttk_LayoutSize(corePtr->layout, corePtr->state, widthPtr, heightPtr);

    /* Assert the -length configuration option */
    Tk_GetPixelsFromObj(NULL, corePtr->tkwin,
	    rangePtr->range.lengthObj, &length);
    if (rangePtr->range.orient == TTK_ORIENT_VERTICAL) {
	*heightPtr = MAX(*heightPtr, length);
    } else {
	*widthPtr = MAX(*widthPtr, length);
    }

    return 1;
}

static double
PointToValue(Range *rangePtr, int x, int y)
{
    Ttk_Box troughBox = TroughRange(rangePtr);
    double from = 0, to = 1, fraction;

    Tcl_GetDoubleFromObj(NULL, rangePtr->range.fromObj, &from);
    Tcl_GetDoubleFromObj(NULL, rangePtr->range.toObj, &to);

    if (rangePtr->range.orient == TTK_ORIENT_HORIZONTAL) {
	fraction = (double)(x - troughBox.x) / (double)troughBox.width;
    } else {
	fraction = (double)(y - troughBox.y) / (double)troughBox.height;
    }

    fraction = fraction < 0 ? 0 : fraction > 1 ? 1 : fraction;

    return from + fraction * (to-from);
}

/*
 * Return the center point in the widget corresponding to the given
 * value. This point can be used to center the slider.
 */

static XPoint
ValueToPoint(Range *rangePtr, double value)
{
    Ttk_Box troughBox = TroughRange(rangePtr);
    double fraction = RangeFraction(rangePtr, value);
    XPoint pt = {0, 0};

    if (rangePtr->range.orient == TTK_ORIENT_HORIZONTAL) {
	pt.x = troughBox.x + (int)(fraction * troughBox.width);
	pt.y = troughBox.y + troughBox.height / 2;
    } else {
	pt.x = troughBox.x + troughBox.width / 2;
	pt.y = troughBox.y + (int)(fraction * troughBox.height);
    }
    return pt;
}

static const Ttk_Ensemble RangeCommands[] = {
    { "configure",   TtkWidgetConfigureCommand,0 },
    { "cget",        TtkWidgetCgetCommand,0 },
    { "state",       TtkWidgetStateCommand,0 },
    { "instate",     TtkWidgetInstateCommand,0 },
    { "identify",    TtkWidgetIdentifyCommand,0 },
    { "setmin",      RangeSetMinCommand,0 },
    { "setmax",      RangeSetMaxCommand,0 },
    { "get",         RangeGetCommand,0 },
    { "getmin",      RangeGetMinCommand,0 },
    { "getmax",      RangeGetMaxCommand,0 },
    { "coords",      RangeCoordsCommand,0 },
    { "mincoords",   RangeMinCoordsCommand,0 },
    { "maxcoords",   RangeMaxCoordsCommand,0 },
    { 0,0,0 }
};

static WidgetSpec RangeWidgetSpec =
{
    "TRange",			/* Class name */
    sizeof(Range),		/* record size */
    RangeOptionSpecs,		/* option specs */
    RangeCommands,		/* widget commands */
    RangeInitialize,		/* initialization proc */
    RangeCleanup,		/* cleanup proc */
    RangeConfigure,		/* configure proc */
    RangePostConfigure,		/* postConfigure */
    RangeGetLayout, 		/* getLayoutProc */
    RangeSize,			/* sizeProc */
    RangeDoLayout,		/* layoutProc */
    TtkWidgetDisplay		/* displayProc */
};

TTK_BEGIN_LAYOUT(VerticalRangeLayout)
    TTK_GROUP("Vertical.Range.trough", TTK_FILL_BOTH,
	TTK_NODE("Vertical.Range.minslider", TTK_PACK_TOP)
	TTK_NODE("Vertical.Range.maxslider", TTK_PACK_BOTTOM) )
TTK_END_LAYOUT

TTK_BEGIN_LAYOUT(HorizontalRangeLayout)
    TTK_GROUP("Horizontal.Range.trough", TTK_FILL_BOTH,
	TTK_NODE("Horizontal.Range.minslider", TTK_PACK_LEFT)
	TTK_NODE("Horizontal.Range.maxslider", TTK_PACK_RIGHT) )
TTK_END_LAYOUT

/*
 * Initialization.
 */
MODULE_SCOPE
void TtkRange_Init(Tcl_Interp *interp)
{
    Ttk_Theme theme = Ttk_GetDefaultTheme(interp);

    Ttk_RegisterLayout(theme, "Vertical.TRange", VerticalRangeLayout);
    Ttk_RegisterLayout(theme, "Horizontal.TRange", HorizontalRangeLayout);

    RegisterWidget(interp, "ttk::range", &RangeWidgetSpec);
}

