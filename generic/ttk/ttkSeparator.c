/* $Id: ttkSeparator.c,v 1.1 2006/10/31 01:42:26 hobbs Exp $
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
	0,(ClientData)TTKOrientStrings,STYLE_CHANGED },

    WIDGET_INHERIT_OPTIONS(CoreOptionSpecs)
};

/*
 * GetLayout hook --
 * 	Choose layout based on -orient option.
 */
static Ttk_Layout SeparatorGetLayout(
    Tcl_Interp *interp, Ttk_Theme theme, void *recordPtr)
{
    Separator *sep = recordPtr;
    return WidgetGetOrientedLayout(
	interp, theme, recordPtr, sep->separator.orientObj);
}

/*
 * Widget commands:
 */
static WidgetCommandSpec SeparatorCommands[] =
{
    { "configure",	WidgetConfigureCommand },
    { "cget",		WidgetCgetCommand },
    { "identify",	WidgetIdentifyCommand },
    { "instate",	WidgetInstateCommand },
    { "state",  	WidgetStateCommand },
    { NULL, NULL }
};

/*
 * Widget specification:
 */
WidgetSpec SeparatorWidgetSpec =
{
    "TSeparator",		/* className */
    sizeof(Separator),		/* recordSize */
    SeparatorOptionSpecs,	/* optionSpecs */
    SeparatorCommands,		/* subcommands */
    NullInitialize,		/* initializeProc */
    NullCleanup,		/* cleanupProc */
    CoreConfigure,		/* configureProc */
    NullPostConfigure,		/* postConfigureProc */
    SeparatorGetLayout,		/* getLayoutProc */
    WidgetSize, 		/* sizeProc */
    WidgetDoLayout,		/* layoutProc */
    WidgetDisplay		/* displayProc */
};

/* +++ Sizegrip widget:
 * 	Has no options or methods other than the standard ones.
 */

static WidgetCommandSpec SizegripCommands[] =
{
    { "configure",	WidgetConfigureCommand },
    { "cget",		WidgetCgetCommand },
    { "identify",	WidgetIdentifyCommand },
    { "instate",	WidgetInstateCommand },
    { "state",  	WidgetStateCommand },
    { NULL, NULL }
};

WidgetSpec SizegripWidgetSpec =
{
    "TSizegrip",		/* className */
    sizeof(WidgetCore),		/* recordSize */
    CoreOptionSpecs, 		/* optionSpecs */
    SizegripCommands,		/* subcommands */
    NullInitialize,		/* initializeProc */
    NullCleanup,		/* cleanupProc */
    CoreConfigure,		/* configureProc */
    NullPostConfigure,		/* postConfigureProc */
    WidgetGetLayout, 		/* getLayoutProc */
    WidgetSize, 		/* sizeProc */
    WidgetDoLayout,		/* layoutProc */
    WidgetDisplay		/* displayProc */
};

/*EOF*/
