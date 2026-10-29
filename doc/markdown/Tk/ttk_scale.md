---
CommandName: ttk::scale
title: ttk::scale
ManualSection: n
Version: 8.5
TclPart: Tk
TclDescription: Tk Themed Widget
Links:
 - ttk::widget(n)
 - scale(n)
Keywords:
 - scale
 - slider
 - trough
 - widget
Copyright:
 - Copyright (c) 2008 Donal Fellows
---

# Name

ttk::scale - Create and manipulate a scale widget

# Synopsis

::: {.synopsis} :::
[ttk::scale]{.cmd} [pathName]{.arg} [options...]{.optarg}
:::

# Description

A **ttk::scale** widget is typically used to control the numeric value of a linked variable that varies uniformly over some range. A scale displays a *slider* that can be moved along over a *trough*, with the relative position of the slider over the trough indicating the value of the variable.

# Standard options

::: {.standardoptions} :::
-class
-cursor
-style
-takefocus
:::

See the [ttk\_widget][ttk_widget] manual entry for details on the standard options.

# Widget-specific options

[-command]{.cmdline} [command]{.dbname} [Command]{.dbclass}
: Specifies the prefix of a Tcl command to invoke whenever the scale's value is changed via a widget command. The actual command consists of this option followed by a space and a real number indicating the new value of the scale.

[-from]{.cmdline} [from]{.dbname} [From]{.dbclass}
: A real value corresponding to the left or top end of the scale.

[-length]{.cmdline} [length]{.dbname} [Length]{.dbclass}
: Specifies the desired long dimension of the scale in screen units (i.e. any of the forms acceptable to [Tk\_GetPixels][GetPixels]). For vertical scales this is the scale's height; for horizontal scales it is the scale's width.

[-orient]{.cmdline} [orient]{.dbname} [Orient]{.dbclass}
: Specifies which orientation whether the widget should be laid out horizontally or vertically. Must be either **horizontal** or **vertical** or an abbreviation of one of these.

[-to]{.cmdline} [to]{.dbname} [To]{.dbclass}
: Specifies a real value corresponding to the right or bottom end of the scale. This value may be either less than or greater than the **-from** option.

[-value]{.cmdline} [value]{.dbname} [Value]{.dbclass}
: Specifies the current floating-point value of the variable. If **-variable** is set to an existing variable, specifying **-value** has no effect (the variable value takes precedence).

[-variable]{.cmdline} [variable]{.dbname} [Variable]{.dbclass}
: Specifies the name of a global variable to link to the scale. Whenever the value of the variable changes, the scale will update to reflect this value. Whenever the scale is manipulated interactively, the variable will be modified to reflect the scale's new value.


# Widget command

In addition to the standard **cget**, **configure**, **identify element**, **instate**, **state** and **style** commands (see [ttk::widget][ttk_widget]), scale widgets support the following additional commands:

[pathName]{.ins} [get]{.sub} [x y]{.optarg}
: Get the current value of the **-value** option, or the value corresponding to the coordinates *x,y* if they are specified. *X* and *y* are pixel coordinates relative to the scale widget origin.

[pathName]{.ins} [set]{.sub} [value]{.arg}
: Set the value of the widget (i.e. the **-value** option) to *value*. The value will be clipped to the range given by the **-from** and **-to** options. Note that setting the linked variable (i.e. the variable named in the **-variable** option) does not cause such clipping.


# Internal commands

[pathName]{.ins} [coords]{.sub} [value]{.optarg}
: Get the coordinates corresponding to *value*, or the coordinates corresponding to the current value of the **-value** option if *value* is omitted.


# Styling options

The class name for a **ttk::scale** is **TScale**.

Dynamic states: **active**.

**TScale** styling options configurable with [ttk::style][ttk_style] are:

**-background** *color* .br **-borderwidth** *amount* .br **-darkcolor** *color* .br **-groovewidth** *amount* .br **-lightcolor** *color* .br **-sliderwidth** *amount* .br **-troughcolor** *color* .br **-troughrelief** *relief*

Some options are only available for specific themes.

See the [ttk::style][ttk_style] manual page for information on how to configure ttk styles.


[GetPixels]: ../TkCAPI/GetPixels.md
[ttk_style]: ttk_style.md
[ttk_widget]: ttk_widget.md

