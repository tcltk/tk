---
CommandName: ttk::radiobutton
title: ttk::radiobutton
ManualSection: n
Version: 8.5
TclPart: Tk
TclDescription: Tk Themed Widget
Links:
 - ttk::widget(n)
 - ttk::checkbutton(n)
 - radiobutton(n)
Keywords:
 - widget
 - button
 - option
Copyright:
 - Copyright (c) 2004 Joe English
---

# Name

ttk::radiobutton - Mutually exclusive option widget

# Synopsis

::: {.synopsis} :::
[ttk::radiobutton]{.cmd} [pathName]{.arg} [options]{.optarg}
:::

# Description

**ttk::radiobutton** widgets are used in groups to show or change a set of mutually-exclusive options. Radiobuttons are linked to a Tcl variable, and have an associated value; when a radiobutton is clicked, it sets the variable to its associated value.

# Standard options

::: {.standardoptions} :::
-class
-compound
-cursor
-image
-state
-style
-takefocus
-text
-textvariable
-underline
-width
:::

See the [ttk\_widget][ttk_widget] manual entry for details on the standard options.

# Widget-specific options

[-command]{.cmdline} [command]{.dbname} [Command]{.dbclass}
: A Tcl script to evaluate whenever the widget is invoked.

[-value]{.cmdline} [Value]{.dbname} [Value]{.dbclass}
: The value to store in the associated **-variable** when the widget is selected.

[-variable]{.cmdline} [variable]{.dbname} [Variable]{.dbclass}
: The name of a global variable whose value is linked to the widget. Default value is **::selectedButton**.


# Widget command

In addition to the standard **cget**, **configure**, **identify element**, **instate**, **state** and **style** commands (see [ttk::widget][ttk_widget]), radiobutton widgets support the following additional commands:

[pathname]{.ins} [invoke]{.sub}
: Sets the **-variable** to the **-value**, selects the widget, and evaluates the associated **-command**. Returns the result of the **-command**, or the empty string if no **-command** is specified.


# Widget states

The widget does not respond to user input if the **disabled** state is set. The widget sets the **selected** state whenever the linked **-variable** is set to the widget's **-value**, and clears it otherwise. The widget sets the **alternate** state whenever the linked **-variable** is unset. (The **alternate** state may be used to indicate a "tri-state" or "indeterminate" selection.)

# Standard styles

**Ttk::radiobutton** widgets support the **Toolbutton** style in all standard themes, which is useful for creating widgets for toolbars.

# Styling options

The class name for a **ttk::radiobutton** is **TRadiobutton**.

Dynamic states: **active**, **alternate**, **disabled**, **pressed**, **readonly**, **selected**.

**TRadiobutton** styling options configurable with [ttk::style][ttk_style] are:

**-background** *color* .br **-compound** *compound* .br **-foreground** *color* .br **-indicatorbackground** *color* .br **-indicatorcolor** *color* .br **-indicatormargin** *padding* .br **-indicatorrelief** *relief* .br **-padding** *padding*

Some options are only available for specific themes.

See the [ttk::style][ttk_style] manual page for information on how to configure ttk styles.


[ttk_style]: ttk_style.md
[ttk_widget]: ttk_widget.md

