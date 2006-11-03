/* $Id: ttkSeparator.c,v 1.2 2006/11/03 03:06:22 das Exp $
 *
 * Copyright (c) 2004, Joe English
 *
 * Ttk widget set: separator and sizegrip widgets.
 */

#include <tk.h>

#include "ttkTheme.h"
#include "ttkWidget.h"

/* +++ Separator widget record:
 */
typedef struct
{
    Tcl_Obj	*orientObj;
    int 	orient;
} SeparatorPart;

typedef struct
{
    WidgetCore core;
    SeparatorPart separator;
} Separator;

static Tk_OptionSpec SeparatorOptionSpecs[] =
{
    {TK_OPTION_STRING_TABLE, "-orient", "orient", "Orient", "horizontal",
	Tk_Offset(Separator,separator.orientObj),
	Tk_Offset(Separator,separator.orient),
	0,(ClientData)ttkOrientStrings,STYLE_CHANGED },

    WIDGET_INHERIT_OPTIONS(ttkCoreOptionSpecs)
};

/*
 * GetLayout hook --
 * 	Choose layout based on -orient option.
 */
static Ttk_Layout SeparatorGetLayout(
    Tcl_Interp *interp, Ttk_Theme theme, void *recordPtr)
{
    Separator *sep = recordPtr;
    return TtkWidgetGetOrientedLayout(
	interp, theme, recordPtr, sep->separator.orientObj);
}

/*
 * Widget commands:
 */
static WidgetCommandSpec SeparatorCommands[] =
{
    { "configure",	TtkWidgetConfigureCommand },
    { "cget",		TtkWidgetCgetCommand },
    { "identify",	TtkWidgetIdentifyCommand },
    { "instate",	TtkWidgetInstateCommand },
    { "state",  	TtkWidgetStateCommand },
    { NULL, NULL }
};

/*
 * Widget specification:
 */
MODULE_SCOPE WidgetSpec ttkSeparatorWidgetSpec;
WidgetSpec ttkSeparatorWidgetSpec =
{
    "TSeparator",		/* className */
    sizeof(Separator),		/* recordSize */
    SeparatorOptionSpecs,	/* optionSpecs */
    SeparatorCommands,		/* subcommands */
    TtkNullInitialize,		/* initializeProc */
    TtkNullCleanup,		/* cleanupProc */
    TtkCoreConfigure,		/* configureProc */
    TtkNullPostConfigure,		/* postConfigureProc */
    SeparatorGetLayout,		/* getLayoutProc */
    TtkWidgetSize, 		/* sizeProc */
    TtkWidgetDoLayout,		/* layoutProc */
    TtkWidgetDisplay		/* displayProc */
};

/* +++ Sizegrip widget:
 * 	Has no options or methods other than the standard ones.
 */

static WidgetCommandSpec SizegripCommands[] =
{
    { "configure",	TtkWidgetConfigureCommand },
    { "cget",		TtkWidgetCgetCommand },
    { "identify",	TtkWidgetIdentifyCommand },
    { "instate",	TtkWidgetInstateCommand },
    { "state",  	TtkWidgetStateCommand },
    { NULL, NULL }
};

MODULE_SCOPE WidgetSpec ttkSizegripWidgetSpec;
WidgetSpec ttkSizegripWidgetSpec =
{
    "TSizegrip",		/* className */
    sizeof(WidgetCore),		/* recordSize */
    ttkCoreOptionSpecs, 		/* optionSpecs */
    SizegripCommands,		/* subcommands */
    TtkNullInitialize,		/* initializeProc */
    TtkNullCleanup,		/* cleanupProc */
    TtkCoreConfigure,		/* configureProc */
    TtkNullPostConfigure,		/* postConfigureProc */
    TtkWidgetGetLayout, 		/* getLayoutProc */
    TtkWidgetSize, 		/* sizeProc */
    TtkWidgetDoLayout,		/* layoutProc */
    TtkWidgetDisplay		/* displayProc */
};

/*EOF*/
