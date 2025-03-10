/*
 * Copyright © 2003, Joe English
 *
 * label, button, checkbutton, radiobutton, and menubutton widgets.
 */

#include "tkInt.h"
#include "ttkThemeInt.h"
#include "ttkWidget.h"

/* Bit fields for OptionSpec mask field:
 */
#define STATE_CHANGED		(0x100)		/* -state option changed */
#define DEFAULTSTATE_CHANGED	(0x200)		/* -default option changed */

/*------------------------------------------------------------------------
 * +++ Base resources for labels, buttons, checkbuttons, etc:
 */
typedef struct
{
    /*
     * Text element resources:
     */
    Tcl_Obj *textObj;
    Tcl_Obj *justifyObj;
    Tcl_Obj *textVariableObj;
    Tcl_Obj *underlineObj;
    Tcl_Obj *widthObj;

    Ttk_TraceHandle	*textVariableTrace;
    Ttk_ImageSpec	*imageSpec;

    /*
     * Image element resources:
     */
    Tcl_Obj *imageObj;

    /*
     * Compound label/image resources:
     */
    Tcl_Obj *compoundObj;
    Tcl_Obj *paddingObj;

    /*
     * Compatibility/legacy options:
     */
    Tcl_Obj *stateObj;

} BasePart;

typedef struct
{
    WidgetCore	core;
    BasePart	base;
} Base;

static const Tk_OptionSpec BaseOptionSpecs[] =
{
    {TK_OPTION_JUSTIFY, "-justify", "justify", "Justify",
	"left", offsetof(Base,base.justifyObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK,0,GEOMETRY_CHANGED },
    {TK_OPTION_STRING, "-text", "text", "Text", "",
	offsetof(Base,base.textObj), TCL_INDEX_NONE,
	0,0,GEOMETRY_CHANGED },
    {TK_OPTION_STRING, "-textvariable", "textVariable", "Variable", "",
	offsetof(Base,base.textVariableObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK,0,GEOMETRY_CHANGED },
    {TK_OPTION_INDEX, "-underline", "underline", "Underline",
	TTK_OPTION_UNDERLINE_DEF(Base, base.underlineObj), 0},
    {TK_OPTION_STRING, "-width", "width", "Width",
	NULL, offsetof(Base,base.widthObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK,0,GEOMETRY_CHANGED },

    /*
     * Image options
     */
    {TK_OPTION_STRING, "-image", "image", "Image", NULL/*default*/,
	offsetof(Base,base.imageObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK,0,GEOMETRY_CHANGED },

    /*
     * Compound base/image options
     */
    {TK_OPTION_STRING_TABLE, "-compound", "compound", "Compound",
	NULL, offsetof(Base,base.compoundObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK, ttkCompoundStrings,
	GEOMETRY_CHANGED },
    {TK_OPTION_STRING, "-padding", "padding", "Pad",
	NULL, offsetof(Base,base.paddingObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK,0,GEOMETRY_CHANGED},

    /*
     * Compatibility/legacy options
     */
    {TK_OPTION_STRING, "-state", "state", "State",
	"normal", offsetof(Base,base.stateObj), TCL_INDEX_NONE,
	0,0,STATE_CHANGED },

    WIDGET_INHERIT_OPTIONS(ttkCoreOptionSpecs)
};

/*
 * Variable trace procedure for -textvariable option:
 */
static void TextVariableChanged(void *clientData, const char *value)
{
    Base *basePtr = (Base *)clientData;
    Tcl_Obj *newText;

    if (WidgetDestroyed(&basePtr->core)) {
	return;
    }

    newText = value ? Tcl_NewStringObj(value, -1) : Tcl_NewStringObj("", 0);

    Tcl_IncrRefCount(newText);
    Tcl_DecrRefCount(basePtr->base.textObj);
    basePtr->base.textObj = newText;

    TtkResizeWidget(&basePtr->core);
}

static void
BaseInitialize(
    TCL_UNUSED(Tcl_Interp *),
    void *recordPtr)
{
    Base *basePtr = (Base *)recordPtr;

    basePtr->base.textVariableTrace = 0;
    basePtr->base.imageSpec = NULL;
}

static void
BaseCleanup(void *recordPtr)
{
    Base *basePtr = (Base *)recordPtr;
    if (basePtr->base.textVariableTrace) {
	Ttk_UntraceVariable(basePtr->base.textVariableTrace);
    }
    if (basePtr->base.imageSpec) {
	TtkFreeImageSpec(basePtr->base.imageSpec);
    }
}

static void
BaseImageChanged(
    void *clientData,
    TCL_UNUSED(int),
    TCL_UNUSED(int),
    TCL_UNUSED(int),
    TCL_UNUSED(int),
    TCL_UNUSED(int),
    TCL_UNUSED(int))
{
    Base *basePtr = (Base *)clientData;

    TtkResizeWidget(&basePtr->core);
}

static int BaseConfigure(Tcl_Interp *interp, void *recordPtr, int mask)
{
    Base *basePtr = (Base *)recordPtr;
    Tcl_Obj *textVarName = basePtr->base.textVariableObj;
    Ttk_TraceHandle *vt = 0;
    Ttk_ImageSpec *imageSpec = NULL;

    if (textVarName != NULL && *Tcl_GetString(textVarName) != '\0') {
	vt = Ttk_TraceVariable(interp,textVarName,TextVariableChanged,basePtr);
	if (!vt) return TCL_ERROR;
    }

    if (basePtr->base.imageObj) {
	imageSpec = TtkGetImageSpecEx(
	    interp, basePtr->core.tkwin, basePtr->base.imageObj, BaseImageChanged, basePtr);
	if (!imageSpec) {
	    goto error;
	}
    }

    if (TtkCoreConfigure(interp, recordPtr, mask) != TCL_OK) {
error:
	if (imageSpec) TtkFreeImageSpec(imageSpec);
	if (vt) Ttk_UntraceVariable(vt);
	return TCL_ERROR;
    }

    if (basePtr->base.textVariableTrace) {
	Ttk_UntraceVariable(basePtr->base.textVariableTrace);
    }
    basePtr->base.textVariableTrace = vt;

    if (basePtr->base.imageSpec) {
	TtkFreeImageSpec(basePtr->base.imageSpec);
    }
    basePtr->base.imageSpec = imageSpec;

    if (mask & STATE_CHANGED) {
	TtkCheckStateOption(&basePtr->core, basePtr->base.stateObj);
    }

    return TCL_OK;
}

static int
BasePostConfigure(
    TCL_UNUSED(Tcl_Interp *),
    void *recordPtr,
    TCL_UNUSED(int))
{
    Base *basePtr = (Base *)recordPtr;
    int status = TCL_OK;

    if (basePtr->base.textVariableTrace) {
	status = Ttk_FireTrace(basePtr->base.textVariableTrace);
    }

    return status;
}

/*------------------------------------------------------------------------
 * +++ Label widget.
 * Just a base widget that adds a few appearance-related options
 */

typedef struct
{
    Tcl_Obj *backgroundObj;
    Tcl_Obj *foregroundObj;
    Tcl_Obj *fontObj;
    Tcl_Obj *borderWidthObj;
    Tcl_Obj *reliefObj;
    Tcl_Obj *anchorObj;
    Tcl_Obj *wrapLengthObj;
} LabelPart;

typedef struct
{
    WidgetCore	core;
    BasePart	base;
    LabelPart	label;
} Label;

static const Tk_OptionSpec LabelOptionSpecs[] =
{
    {TK_OPTION_BORDER, "-background", "frameColor", "FrameColor",
	NULL, offsetof(Label,label.backgroundObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK,0,0 },
    {TK_OPTION_COLOR, "-foreground", "textColor", "TextColor",
	NULL, offsetof(Label,label.foregroundObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK,0,0 },
    {TK_OPTION_FONT, "-font", "font", "Font",
	NULL, offsetof(Label,label.fontObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK,0,GEOMETRY_CHANGED },
    {TK_OPTION_PIXELS, "-borderwidth", "borderWidth", "BorderWidth",
	NULL, offsetof(Label,label.borderWidthObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK,0,GEOMETRY_CHANGED },
    {TK_OPTION_RELIEF, "-relief", "relief", "Relief",
	NULL, offsetof(Label,label.reliefObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK,0,GEOMETRY_CHANGED },
    {TK_OPTION_ANCHOR, "-anchor", "anchor", "Anchor",
	"w", offsetof(Label,label.anchorObj), TCL_INDEX_NONE,
	0, 0, GEOMETRY_CHANGED},
    {TK_OPTION_PIXELS, "-wraplength", "wrapLength", "WrapLength",
	NULL, offsetof(Label, label.wrapLengthObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK,0,GEOMETRY_CHANGED /*SB: SIZE_CHANGED*/ },

    WIDGET_TAKEFOCUS_FALSE,
    WIDGET_INHERIT_OPTIONS(BaseOptionSpecs)
};

static const Ttk_Ensemble LabelCommands[] = {
    { "cget",		TtkWidgetCgetCommand,0 },
    { "configure",	TtkWidgetConfigureCommand,0 },
    { "identify",	TtkWidgetIdentifyCommand,0 },
    { "instate",	TtkWidgetInstateCommand,0 },
    { "state",	TtkWidgetStateCommand,0 },
    { "style",		TtkWidgetStyleCommand,0 },
    { 0,0,0 }
};

static const WidgetSpec LabelWidgetSpec =
{
    "TLabel",			/* className */
    sizeof(Label),		/* recordSize */
    LabelOptionSpecs,		/* optionSpecs */
    LabelCommands,		/* subcommands */
    BaseInitialize,		/* initializeProc */
    BaseCleanup,		/* cleanupProc */
    BaseConfigure,		/* configureProc */
    BasePostConfigure,		/* postConfigureProc */
    TtkWidgetGetLayout,	/* getLayoutProc */
    TtkWidgetSize,		/* sizeProc */
    TtkWidgetDoLayout,		/* layoutProc */
    TtkWidgetDisplay		/* displayProc */
};

TTK_BEGIN_LAYOUT(LabelLayout)
    TTK_GROUP("Label.border", TTK_FILL_BOTH|TTK_BORDER,
	TTK_GROUP("Label.padding", TTK_FILL_BOTH|TTK_BORDER,
	    TTK_NODE("Label.label", TTK_FILL_BOTH)))
TTK_END_LAYOUT

/*------------------------------------------------------------------------
 * +++ Button widget.
 * Adds a new subcommand "invoke", and options "-command" and "-default"
 */

typedef struct
{
    Tcl_Obj *commandObj;
    Tcl_Obj *defaultStateObj;
} ButtonPart;

typedef struct
{
    WidgetCore	core;
    BasePart	base;
    ButtonPart	button;
} Button;

/*
 * Option specifications:
 */
static const Tk_OptionSpec ButtonOptionSpecs[] =
{
    {TK_OPTION_STRING, "-command", "command", "Command",
	"", offsetof(Button, button.commandObj), TCL_INDEX_NONE, 0,0,0},
    {TK_OPTION_STRING_TABLE, "-default", "default", "Default",
	"normal", offsetof(Button, button.defaultStateObj), TCL_INDEX_NONE,
	0, ttkDefaultStrings, DEFAULTSTATE_CHANGED},

    WIDGET_TAKEFOCUS_TRUE,
    WIDGET_INHERIT_OPTIONS(BaseOptionSpecs)
};

static int ButtonConfigure(Tcl_Interp *interp, void *recordPtr, int mask)
{
    Button *buttonPtr = (Button *)recordPtr;

    if (BaseConfigure(interp, recordPtr, mask) != TCL_OK) {
	return TCL_ERROR;
    }

    /* Handle "-default" option:
     */
    if (mask & DEFAULTSTATE_CHANGED) {
	Ttk_ButtonDefaultState defaultState = TTK_BUTTON_DEFAULT_DISABLED;
	Ttk_GetButtonDefaultStateFromObj(
	    NULL, buttonPtr->button.defaultStateObj, &defaultState);
	if (defaultState == TTK_BUTTON_DEFAULT_ACTIVE) {
	    TtkWidgetChangeState(&buttonPtr->core, TTK_STATE_ALTERNATE, 0);
	} else {
	    TtkWidgetChangeState(&buttonPtr->core, 0, TTK_STATE_ALTERNATE);
	}
    }
    return TCL_OK;
}

/* $button invoke --
 *	Evaluate the button's -command.
 */
static int
ButtonInvokeCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Button *buttonPtr = (Button *)recordPtr;
    if (objc > 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "invoke");
	return TCL_ERROR;
    }
    if (buttonPtr->core.state & TTK_STATE_DISABLED) {
	return TCL_OK;
    }
    return Tcl_EvalObjEx(interp, buttonPtr->button.commandObj, TCL_EVAL_GLOBAL);
}

static const Ttk_Ensemble ButtonCommands[] = {
    { "cget",		TtkWidgetCgetCommand,0 },
    { "configure",	TtkWidgetConfigureCommand,0 },
    { "identify",	TtkWidgetIdentifyCommand,0 },
    { "instate",	TtkWidgetInstateCommand,0 },
    { "invoke",		ButtonInvokeCommand,0 },
    { "state",	TtkWidgetStateCommand,0 },
    { "style",		TtkWidgetStyleCommand,0 },
    { 0,0,0 }
};

static const WidgetSpec ButtonWidgetSpec =
{
    "TButton",			/* className */
    sizeof(Button),		/* recordSize */
    ButtonOptionSpecs,		/* optionSpecs */
    ButtonCommands,		/* subcommands */
    BaseInitialize,		/* initializeProc */
    BaseCleanup,		/* cleanupProc */
    ButtonConfigure,		/* configureProc */
    BasePostConfigure,		/* postConfigureProc */
    TtkWidgetGetLayout,		/* getLayoutProc */
    TtkWidgetSize,		/* sizeProc */
    TtkWidgetDoLayout,		/* layoutProc */
    TtkWidgetDisplay		/* displayProc */
};

TTK_BEGIN_LAYOUT(ButtonLayout)
    TTK_GROUP("Button.border", TTK_FILL_BOTH|TTK_BORDER,
	TTK_GROUP("Button.focus", TTK_FILL_BOTH,
	    TTK_GROUP("Button.padding", TTK_FILL_BOTH,
		TTK_NODE("Button.label", TTK_FILL_BOTH))))
TTK_END_LAYOUT

/*------------------------------------------------------------------------
 * +++ Checkbutton widget.
 */
typedef struct
{
    Tcl_Obj *variableObj;
    Tcl_Obj *onValueObj;
    Tcl_Obj *offValueObj;
    Tcl_Obj *commandObj;

    Ttk_TraceHandle *variableTrace;

} CheckbuttonPart;

typedef struct
{
    WidgetCore core;
    BasePart base;
    CheckbuttonPart checkbutton;
} Checkbutton;

/*
 * Option specifications:
 */
static const Tk_OptionSpec CheckbuttonOptionSpecs[] =
{
    {TK_OPTION_STRING, "-variable", "variable", "Variable",
	NULL, offsetof(Checkbutton, checkbutton.variableObj), TCL_INDEX_NONE,
	TK_OPTION_NULL_OK,0,0},
    {TK_OPTION_STRING, "-onvalue", "onValue", "OnValue",
	"1", offsetof(Checkbutton, checkbutton.onValueObj), TCL_INDEX_NONE,
	0,0,0},
    {TK_OPTION_STRING, "-offvalue", "offValue", "OffValue",
	"0", offsetof(Checkbutton, checkbutton.offValueObj), TCL_INDEX_NONE,
	0,0,0},
    {TK_OPTION_STRING, "-command", "command", "Command",
	"", offsetof(Checkbutton, checkbutton.commandObj), TCL_INDEX_NONE,
	0,0,0},

    WIDGET_TAKEFOCUS_TRUE,
    WIDGET_INHERIT_OPTIONS(BaseOptionSpecs)
};

/*
 * Variable trace procedure for checkbutton -variable option
 */
static void CheckbuttonVariableChanged(void *clientData, const char *value)
{
    Checkbutton *checkPtr = (Checkbutton *)clientData;

    if (WidgetDestroyed(&checkPtr->core)) {
	return;
    }

    if (!value) {
	TtkWidgetChangeState(&checkPtr->core, TTK_STATE_ALTERNATE, 0);
	return;
    }
    /* else */
    TtkWidgetChangeState(&checkPtr->core, 0, TTK_STATE_ALTERNATE);
    if (!strcmp(value, Tcl_GetString(checkPtr->checkbutton.onValueObj))) {
	TtkWidgetChangeState(&checkPtr->core, TTK_STATE_SELECTED, 0);
    } else {
	TtkWidgetChangeState(&checkPtr->core, 0, TTK_STATE_SELECTED);
    }
}

static void
CheckbuttonInitialize(Tcl_Interp *interp, void *recordPtr)
{
    Checkbutton *checkPtr = (Checkbutton *)recordPtr;
    Tcl_Obj *variableObj;

    /* default -variable is the widget name:
     */
    variableObj = Tcl_NewStringObj(Tk_PathName(checkPtr->core.tkwin), -1);
    Tcl_IncrRefCount(variableObj);
    checkPtr->checkbutton.variableObj = variableObj;
    BaseInitialize(interp, recordPtr);
}

static void
CheckbuttonCleanup(void *recordPtr)
{
    Checkbutton *checkPtr = (Checkbutton *)recordPtr;
    Ttk_UntraceVariable(checkPtr->checkbutton.variableTrace);
    checkPtr->checkbutton.variableTrace = 0;
    BaseCleanup(recordPtr);
}

static int
CheckbuttonConfigure(Tcl_Interp *interp, void *recordPtr, int mask)
{
    Checkbutton *checkPtr = (Checkbutton *)recordPtr;
    Tcl_Obj *varName = checkPtr->checkbutton.variableObj;
    Ttk_TraceHandle *vt = NULL;

    if (varName != NULL && *Tcl_GetString(varName) != '\0') {
	vt = Ttk_TraceVariable(interp, varName,
	    CheckbuttonVariableChanged, checkPtr);
	if (!vt) {
	    return TCL_ERROR;
	}
    }

    if (BaseConfigure(interp, recordPtr, mask) != TCL_OK){
	Ttk_UntraceVariable(vt);
	return TCL_ERROR;
    }

    if (checkPtr->checkbutton.variableTrace) {
	Ttk_UntraceVariable(checkPtr->checkbutton.variableTrace);
    }
    checkPtr->checkbutton.variableTrace = vt;

    return TCL_OK;
}

static int
CheckbuttonPostConfigure(Tcl_Interp *interp, void *recordPtr, int mask)
{
    Checkbutton *checkPtr = (Checkbutton *)recordPtr;
    int status = TCL_OK;

    if (checkPtr->checkbutton.variableTrace)
	status = Ttk_FireTrace(checkPtr->checkbutton.variableTrace);
    if (status == TCL_OK && !WidgetDestroyed(&checkPtr->core))
	status = BasePostConfigure(interp, recordPtr, mask);
    return status;
}

/*
 * Checkbutton 'invoke' subcommand:
 *	Toggles the checkbutton state.
 */
static int
CheckbuttonInvokeCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Checkbutton *checkPtr = (Checkbutton *)recordPtr;
    WidgetCore *corePtr = &checkPtr->core;
    Tcl_Obj *newValue;

    if (objc > 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "invoke");
	return TCL_ERROR;
    }
    if (corePtr->state & TTK_STATE_DISABLED)
	return TCL_OK;

    /*
     * Toggle the selected state.
     */
    if (corePtr->state & TTK_STATE_SELECTED)
	newValue = checkPtr->checkbutton.offValueObj;
    else
	newValue = checkPtr->checkbutton.onValueObj;

    if (checkPtr->checkbutton.variableObj == NULL ||
	*Tcl_GetString(checkPtr->checkbutton.variableObj) == '\0')
	CheckbuttonVariableChanged(checkPtr, Tcl_GetString(newValue));
    else if (Tcl_ObjSetVar2(interp,
		checkPtr->checkbutton.variableObj, NULL, newValue,
		TCL_GLOBAL_ONLY|TCL_LEAVE_ERR_MSG)
	    == NULL)
	return TCL_ERROR;

    if (WidgetDestroyed(corePtr))
	return TCL_ERROR;

    return Tcl_EvalObjEx(interp,
	checkPtr->checkbutton.commandObj, TCL_EVAL_GLOBAL);
}

static const Ttk_Ensemble CheckbuttonCommands[] = {
    { "cget",		TtkWidgetCgetCommand,0 },
    { "configure",	TtkWidgetConfigureCommand,0 },
    { "identify",	TtkWidgetIdentifyCommand,0 },
    { "instate",	TtkWidgetInstateCommand,0 },
    { "invoke",		CheckbuttonInvokeCommand,0 },
    { "state",	TtkWidgetStateCommand,0 },
    { "style",		TtkWidgetStyleCommand,0 },
    /* MISSING: select, deselect, toggle */
    { 0,0,0 }
};

static const WidgetSpec CheckbuttonWidgetSpec =
{
    "TCheckbutton",		/* className */
    sizeof(Checkbutton),	/* recordSize */
    CheckbuttonOptionSpecs,	/* optionSpecs */
    CheckbuttonCommands,	/* subcommands */
    CheckbuttonInitialize,	/* initializeProc */
    CheckbuttonCleanup,		/* cleanupProc */
    CheckbuttonConfigure,	/* configureProc */
    CheckbuttonPostConfigure,	/* postConfigureProc */
    TtkWidgetGetLayout,	/* getLayoutProc */
    TtkWidgetSize,		/* sizeProc */
    TtkWidgetDoLayout,		/* layoutProc */
    TtkWidgetDisplay		/* displayProc */
};

TTK_BEGIN_LAYOUT(CheckbuttonLayout)
     TTK_GROUP("Checkbutton.padding", TTK_FILL_BOTH,
	 TTK_NODE("Checkbutton.indicator", TTK_PACK_LEFT)
	 TTK_GROUP("Checkbutton.focus", TTK_PACK_LEFT | TTK_STICK_W,
	     TTK_NODE("Checkbutton.label", TTK_FILL_BOTH)))
TTK_END_LAYOUT

/*------------------------------------------------------------------------
 * +++ Radiobutton widget.
 */

typedef struct
{
    Tcl_Obj *variableObj;
    Tcl_Obj *valueObj;
    Tcl_Obj *commandObj;

    Ttk_TraceHandle	*variableTrace;

} RadiobuttonPart;

typedef struct
{
    WidgetCore core;
    BasePart base;
    RadiobuttonPart radiobutton;
} Radiobutton;

/*
 * Option specifications:
 */
static const Tk_OptionSpec RadiobuttonOptionSpecs[] =
{
    {TK_OPTION_STRING, "-variable", "variable", "Variable",
	"::selectedButton", offsetof(Radiobutton, radiobutton.variableObj),TCL_INDEX_NONE,
	0,0,0},
    {TK_OPTION_STRING, "-value", "Value", "Value",
	"1", offsetof(Radiobutton, radiobutton.valueObj), TCL_INDEX_NONE,
	0,0,0},
    {TK_OPTION_STRING, "-command", "command", "Command",
	"", offsetof(Radiobutton, radiobutton.commandObj), TCL_INDEX_NONE,
	0,0,0},

    WIDGET_TAKEFOCUS_TRUE,
    WIDGET_INHERIT_OPTIONS(BaseOptionSpecs)
};

/*
 * Variable trace procedure for radiobuttons.
 */
static void
RadiobuttonVariableChanged(void *clientData, const char *value)
{
    Radiobutton *radioPtr = (Radiobutton *)clientData;

    if (WidgetDestroyed(&radioPtr->core)) {
	return;
    }

    if (!value) {
	TtkWidgetChangeState(&radioPtr->core, TTK_STATE_ALTERNATE, 0);
	return;
    }
    /* else */
    TtkWidgetChangeState(&radioPtr->core, 0, TTK_STATE_ALTERNATE);
    if (!strcmp(value, Tcl_GetString(radioPtr->radiobutton.valueObj))) {
	TtkWidgetChangeState(&radioPtr->core, TTK_STATE_SELECTED, 0);
    } else {
	TtkWidgetChangeState(&radioPtr->core, 0, TTK_STATE_SELECTED);
    }
}

static void
RadiobuttonCleanup(void *recordPtr)
{
    Radiobutton *radioPtr = (Radiobutton *)recordPtr;
    Ttk_UntraceVariable(radioPtr->radiobutton.variableTrace);
    radioPtr->radiobutton.variableTrace = 0;
    BaseCleanup(recordPtr);
}

static int
RadiobuttonConfigure(Tcl_Interp *interp, void *recordPtr, int mask)
{
    Radiobutton *radioPtr = (Radiobutton *)recordPtr;
    Ttk_TraceHandle *vt = Ttk_TraceVariable(
	interp, radioPtr->radiobutton.variableObj,
	RadiobuttonVariableChanged, radioPtr);

    if (!vt) {
	return TCL_ERROR;
    }

    if (BaseConfigure(interp, recordPtr, mask) != TCL_OK) {
	Ttk_UntraceVariable(vt);
	return TCL_ERROR;
    }

    Ttk_UntraceVariable(radioPtr->radiobutton.variableTrace);
    radioPtr->radiobutton.variableTrace = vt;

    return TCL_OK;
}

static int
RadiobuttonPostConfigure(Tcl_Interp *interp, void *recordPtr, int mask)
{
    Radiobutton *radioPtr = (Radiobutton *)recordPtr;
    int status = TCL_OK;

    if (radioPtr->radiobutton.variableTrace)
	status = Ttk_FireTrace(radioPtr->radiobutton.variableTrace);
    if (status == TCL_OK && !WidgetDestroyed(&radioPtr->core))
	status = BasePostConfigure(interp, recordPtr, mask);
    return status;
}

/*
 * Radiobutton 'invoke' subcommand:
 *	Sets the radiobutton -variable to the -value, evaluates the -command.
 */
static int
RadiobuttonInvokeCommand(
    void *recordPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[])
{
    Radiobutton *radioPtr = (Radiobutton *)recordPtr;
    WidgetCore *corePtr = &radioPtr->core;

    if (objc > 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "invoke");
	return TCL_ERROR;
    }
    if (corePtr->state & TTK_STATE_DISABLED)
	return TCL_OK;

    if (Tcl_ObjSetVar2(interp,
	    radioPtr->radiobutton.variableObj, NULL,
	    radioPtr->radiobutton.valueObj,
	    TCL_GLOBAL_ONLY|TCL_LEAVE_ERR_MSG)
	== NULL)
	return TCL_ERROR;

    if (WidgetDestroyed(corePtr))
	return TCL_ERROR;

    return Tcl_EvalObjEx(interp,
	radioPtr->radiobutton.commandObj, TCL_EVAL_GLOBAL);
}

static const Ttk_Ensemble RadiobuttonCommands[] = {
    { "cget",		TtkWidgetCgetCommand,0 },
    { "configure",	TtkWidgetConfigureCommand,0 },
    { "identify",	TtkWidgetIdentifyCommand,0 },
    { "instate",	TtkWidgetInstateCommand,0 },
    { "invoke",		RadiobuttonInvokeCommand,0 },
    { "state",	TtkWidgetStateCommand,0 },
    { "style",		TtkWidgetStyleCommand,0 },
    /* MISSING: select, deselect */
    { 0,0,0 }
};

static const WidgetSpec RadiobuttonWidgetSpec =
{
    "TRadiobutton",		/* className */
    sizeof(Radiobutton),	/* recordSize */
    RadiobuttonOptionSpecs,	/* optionSpecs */
    RadiobuttonCommands,	/* subcommands */
    BaseInitialize,		/* initializeProc */
    RadiobuttonCleanup,		/* cleanupProc */
    RadiobuttonConfigure,	/* configureProc */
    RadiobuttonPostConfigure,	/* postConfigureProc */
    TtkWidgetGetLayout,	/* getLayoutProc */
    TtkWidgetSize,		/* sizeProc */
    TtkWidgetDoLayout,		/* layoutProc */
    TtkWidgetDisplay		/* displayProc */
};

TTK_BEGIN_LAYOUT(RadiobuttonLayout)
     TTK_GROUP("Radiobutton.padding", TTK_FILL_BOTH,
	 TTK_NODE("Radiobutton.indicator", TTK_PACK_LEFT)
	 TTK_GROUP("Radiobutton.focus", TTK_PACK_LEFT,
	     TTK_NODE("Radiobutton.label", TTK_FILL_BOTH)))
TTK_END_LAYOUT

/*------------------------------------------------------------------------
 * +++ Menubutton widget.
 */

typedef struct
{
    Tcl_Obj *menuObj;
    Tcl_Obj *directionObj;
} MenubuttonPart;

typedef struct
{
    WidgetCore core;
    BasePart base;
    MenubuttonPart menubutton;
} Menubutton;

/*
 * Option specifications:
 */
static const char *const directionStrings[] = {
    "above", "below", "flush", "left", "right", NULL
};
static const Tk_OptionSpec MenubuttonOptionSpecs[] =
{
    {TK_OPTION_STRING, "-menu", "menu", "Menu",
	"", offsetof(Menubutton, menubutton.menuObj), TCL_INDEX_NONE, 0,0,0},
    {TK_OPTION_STRING_TABLE, "-direction", "direction", "Direction",
	"below", offsetof(Menubutton, menubutton.directionObj), TCL_INDEX_NONE,
	0, directionStrings, GEOMETRY_CHANGED},

    WIDGET_TAKEFOCUS_TRUE,
    WIDGET_INHERIT_OPTIONS(BaseOptionSpecs)
};

static const Ttk_Ensemble MenubuttonCommands[] = {
    { "configure",	TtkWidgetConfigureCommand,0 },
    { "cget",		TtkWidgetCgetCommand,0 },
    { "instate",	TtkWidgetInstateCommand,0 },
    { "state",	TtkWidgetStateCommand,0 },
    { "identify",	TtkWidgetIdentifyCommand,0 },
    { "style",		TtkWidgetStyleCommand,0 },
    { 0,0,0 }
};

static const WidgetSpec MenubuttonWidgetSpec =
{
    "TMenubutton",		/* className */
    sizeof(Menubutton),	/* recordSize */
    MenubuttonOptionSpecs,	/* optionSpecs */
    MenubuttonCommands,	/* subcommands */
    BaseInitialize,		/* initializeProc */
    BaseCleanup,		/* cleanupProc */
    BaseConfigure,		/* configureProc */
    BasePostConfigure,	/* postConfigureProc */
    TtkWidgetGetLayout,	/* getLayoutProc */
    TtkWidgetSize,		/* sizeProc */
    TtkWidgetDoLayout,		/* layoutProc */
    TtkWidgetDisplay		/* displayProc */
};

TTK_BEGIN_LAYOUT(MenubuttonLayout)
    TTK_GROUP("Menubutton.border", TTK_FILL_BOTH,
	TTK_GROUP("Menubutton.focus", TTK_FILL_BOTH,
	    TTK_NODE("Menubutton.indicator", TTK_PACK_RIGHT)
	    TTK_GROUP("Menubutton.padding", TTK_FILL_X,
		TTK_NODE("Menubutton.label", TTK_PACK_LEFT))))
TTK_END_LAYOUT

/*------------------------------------------------------------------------
 * +++ Initialization.
 */

MODULE_SCOPE void
TtkButton_Init(Tcl_Interp *interp)
{
    Ttk_Theme theme = Ttk_GetDefaultTheme(interp);

    Ttk_RegisterLayout(theme, "TLabel", LabelLayout);
    Ttk_RegisterLayout(theme, "TButton", ButtonLayout);
    Ttk_RegisterLayout(theme, "TCheckbutton", CheckbuttonLayout);
    Ttk_RegisterLayout(theme, "TRadiobutton", RadiobuttonLayout);
    Ttk_RegisterLayout(theme, "TMenubutton", MenubuttonLayout);

    RegisterWidget(interp, "ttk::label", &LabelWidgetSpec);
    RegisterWidget(interp, "ttk::button", &ButtonWidgetSpec);
    RegisterWidget(interp, "ttk::checkbutton", &CheckbuttonWidgetSpec);
    RegisterWidget(interp, "ttk::radiobutton", &RadiobuttonWidgetSpec);
    RegisterWidget(interp, "ttk::menubutton", &MenubuttonWidgetSpec);
}
