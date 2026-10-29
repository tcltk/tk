---
CommandName: tk_setPalette
title: tk_setPalette
ManualSection: n
Version: 4.0
TclPart: Tk
TclDescription: Tk Built-In Commands
Keywords:
 - bisque
 - color
 - palette
Copyright:
 - Copyright (c) 1995-1996 Sun Microsystems, Inc.
---

# Name

tk\_setPalette, tk\_bisque - Modify the Tk color palette

# Synopsis

::: {.synopsis} :::
[tk\_setPalette]{.cmd} [background]{.arg}
[tk\_setPalette]{.cmd} [name]{.arg} [value]{.arg} [name value]{.optdot}
[tk\_bisque]{.cmd}
:::

# Description

The **tk\_setPalette** procedure changes the color scheme for Tk. It does this by modifying the colors of existing widgets and by changing the option database so that future widgets will use the new color scheme. If **tk\_setPalette** is invoked with a single argument, the argument is the name of a color to use as the normal background color;  **tk\_setPalette** will compute a complete color palette from this background color. Alternatively, the arguments to **tk\_setPalette** may consist of any number of *name*-*value* pairs, where the first argument of the pair is the name of an option in the Tk option database and the second argument is the new value to use for that option.  The following database names are currently supported:

::: {.info DISPLAY="yes"}
**activeBackground**	**foreground**	**selectColor** **activeForeground**	**highlightBackground**	**selectBackground** **background**	**highlightColor**	**selectForeground** **disabledForeground**	**insertBackground**	**troughColor**
:::

**tk\_setPalette** tries to compute reasonable defaults for any options that you do not specify.  You can specify options other than the above ones and Tk will change those options on widgets as well.  This feature may be useful if you are using custom widgets with additional color options.

Once it has computed the new value to use for each of the color options, **tk\_setPalette** scans the widget hierarchy to modify the options of all existing widgets.  For each widget, it checks to see if any of the above options is defined for the widget.  If so, and if the option's current value is the default, then the value is changed;  if the option has a value other than the default, **tk\_setPalette** will not change it.  The default for an option is the one provided by the widget (**\[lindex \[$w configure $option\] 3\]**) unless **tk\_setPalette** has been run previously, in which case it is the value specified in the previous invocation of **tk\_setPalette**.

After modifying all the widgets in the application, **tk\_setPalette** adds options to the option database to change the defaults for widgets created in the future.  The new options are added at priority **widgetDefault**, so they will be overridden by options from the .Xdefaults file or options specified on the command-line that creates a widget.

The procedure **tk\_bisque** is provided for backward compatibility: it restores the application's colors to the light brown ("bisque") color scheme used in Tk 3.6 and earlier versions.

