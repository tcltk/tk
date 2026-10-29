---
CommandName: ttk::panedwindow
title: ttk::panedwindow
ManualSection: n
Version: 8.5
TclPart: Tk
TclDescription: Tk Themed Widget
Links:
 - ttk::widget(n)
 - ttk::notebook(n)
 - panedwindow(n)
Copyright:
 - Copyright (c) 2005 Joe English
---

# Name

ttk::panedwindow - Multi-pane container window

# Synopsis

::: {.synopsis} :::
[ttk::panedwindow]{.cmd} [pathname]{.arg} [options]{.optarg}
[pathname]{.ins} [add]{.sub} [window]{.arg} [options...]{.optarg}
[pathname]{.ins} [insert]{.sub} [index]{.arg} [window]{.arg} [options...]{.optarg}
:::

# Description

A **ttk::panedwindow** widget displays a number of subwindows, stacked either vertically or horizontally. The user may adjust the relative sizes of the subwindows by dragging the sash between panes.

If a pane is not a child of the panedwindow, it must be higher in the stacking order than the panedwindow, otherwise the panedwindow will obscure it.  The easiest way to ensure this is to create the panedwindow first: the most recently created window is highest in the stacking order. Or, you can use the [raise] and [lower] commands.

# Standard options

::: {.standardoptions} :::
-class
-cursor
-style
-takefocus
:::

See the [ttk\_widget][ttk_widget] manual entry for details on the standard options.

# Widget-specific options

[-orient]{.cmdline} [orient]{.dbname} [Orient]{.dbclass}
: Specifies the orientation of the window. If **vertical**, subpanes are stacked top-to-bottom; if **horizontal**, subpanes are stacked left-to-right.

[-width]{.cmdline} [width]{.dbname} [Width]{.dbclass}
: If present and greater than zero, specifies the desired width of the widget in pixels. Otherwise, the requested width is determined by the width of the managed windows.

[-height]{.cmdline} [height]{.dbname} [Height]{.dbclass}
: If present and greater than zero, specifies the desired height of the widget in pixels. Otherwise, the requested height is determined by the height of the managed windows.


# Pane options

The following options may be specified for each pane:

[-weight]{.cmdline} [weight]{.dbname} [Weight]{.dbclass}
: An integer specifying the relative stretchability of the pane. When the paned window is resized, the extra space is added or subtracted to each pane proportionally to its **-weight**.


# Widget command

In addition to the standard **cget**, **configure**, **instate**, **state** and **style** commands (see [ttk::widget][ttk_widget]), panedwindow widgets support the following additional commands:

[pathname]{.ins} [add]{.sub} [subwindow]{.arg} [options...]{.arg}
: Adds a new pane to the window. See [Pane options] for the list of available options.

[pathname]{.ins} [forget]{.sub} [pane]{.arg}
: Removes the specified subpane from the widget. *pane* is either an integer index or the name of a managed subwindow.

[pathname]{.ins} [identify]{.sub} [component]{.arg} [x]{.arg} [y]{.arg}
: Returns the name of the element under the point given by *x* and *y*, or the empty string if no component is present at that location. If *component* is omitted, it defaults to **sash**. The following subcommands are supported:

    [pathname]{.ins} [identify]{.sub} [element]{.lit} [x]{.arg} [y]{.arg}
    : Returns the name of the element at the specified location.

    *pathname* **identify sash** *x y*
    : Returns the index of the sash at the specified location.


*pathname* **insert** *pos subwindow options...*
: Inserts a pane at the specified position. *pos* is either the string **end**, an integer index, or the name of a managed subwindow. If *subwindow* is already managed by the paned window, moves it to the specified position. See [Pane options] for the list of available options.

[pathname]{.ins} [pane]{.sub} [pane]{.arg}
: see below ...

*pathname* **pane** *pane -option*
: see below ...

*pathname* **pane** *pane -option value* ?*-option value ...*?
: Query or modify the options of the specified *pane*, where *pane* is either an integer index or the name of a managed subwindow. In its first form, when no *-option* is specified, the command returns a dictionary of the pane option values. In its second form, when one *-option* is specified, returns the value of that *option*. Otherwise, in its third form, sets the *-option*s to the corresponding *value*s.

[pathname]{.ins} [panes]{.sub}
: Returns the list of all windows managed by the widget, in the index order of their associated panes.

[pathname]{.ins} [sashpos]{.sub} [index]{.arg} [newpos]{.optarg}
: If *newpos* is specified, sets the position of sash number *index*. May adjust the positions of adjacent sashes to ensure that positions are monotonically increasing. Sash positions are further constrained to be between 0 and the total size of the widget. Returns the new position of sash number *index*.


# Virtual events

The panedwindow widget generates an **<<EnteredChild>>** virtual event on LeaveNotify/NotifyInferior events.

# Styling options

The class name for a **ttk::panedwindow** is **TPanedwindow**.  The sash has a class name of **Sash**.

**TPanedwindow** styling options configurable with [ttk::style][ttk_style] are:

**-background** *color*

**Sash** styling options configurable with [ttk::style][ttk_style] are:

**-background** *color* .br **-bordercolor** *color* .br **-gripsize** *size* (number of screen units) .br **-handlepad** *amount* .br **-handlesize** *amount* .br **-lightcolor** *color* .br **-sashpad** *amount* .br **-sashrelief** *relief* .br **-sashthickness** *amount*

Some options are only available for specific themes.

See the [ttk::style][ttk_style] manual page for information on how to configure ttk styles.


[lower]: lower.md
[raise]: raise.md
[ttk_style]: ttk_style.md
[ttk_widget]: ttk_widget.md

