---
CommandName: ttk::checkbutton
title: ttk::checkbutton
ManualSection: n
Version: 8.5
TclPart: Tk
TclDescription: Tk Themed Widget
Links:
 - ttk::widget(n)
 - ttk::radiobutton(n)
 - checkbutton(n)
Keywords:
 - widget
 - button
 - toggle
 - check
 - option
Copyright:
 - Copyright (c) 2004 Joe English
---

# Name

ttk::checkbutton - On/off widget

# Synopsis

::: {.synopsis} :::
[ttk::checkbutton]{.cmd} [pathName]{.arg} [options]{.optarg}
:::

# Description

A **ttk::checkbutton** widget is used to show or change a setting. It has two states, selected and deselected. The state of the checkbutton may be linked to a Tcl variable.

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
: A Tcl script to execute whenever the widget is invoked.

[-offvalue]{.cmdline} [offValue]{.dbname} [OffValue]{.dbclass}
: The value to store in the associated **-variable** when the widget is deselected.  Defaults to **0**.

[-onvalue]{.cmdline} [onValue]{.dbname} [OnValue]{.dbclass}
: The value to store in the associated **-variable** when the widget is selected.  Defaults to **1**.

[-variable]{.cmdline} [variable]{.dbname} [Variable]{.dbclass}
: The name of a global variable whose value is linked to the widget. Defaults to the widget pathname if not specified.


# Widget command

In addition to the standard **cget**, **configure**, **identify element**, **instate**, **state** and **style** commands (see [ttk::widget][ttk_widget]), checkbutton widgets support the following additional commands:

[pathname]{.ins} [invoke]{.sub}
: Toggles between the selected and deselected states and evaluates the associated **-command**. If the widget is currently selected, sets the **-variable** to the **-offvalue** and deselects the widget; otherwise, sets the **-variable** to the **-onvalue** Returns the result of the **-command**.


# Widget states

The widget does not respond to user input if the **disabled** state is set. The widget sets the **selected** state whenever the linked **-variable** is set to the widget's **-onvalue**, and clears it otherwise. The widget sets the **alternate** state whenever the linked **-variable** is unset. (The **alternate** state may be used to indicate a "tri-state" or "indeterminate" selection.)

# Standard styles

**Ttk::checkbutton** widgets support the **Toolbutton** style in all standard themes, which is useful for creating widgets for toolbars.

# Styling options

The class name for a **ttk::checkbutton** is **TCheckbutton**.

Dynamic states: **active**, **alternate**, **disabled**, **pressed**, **selected**, **readonly**.

**TCheckbutton** styling options configurable with [ttk::style][ttk_style] are:

**-background** *color* .br **-compound** *compound* .br **-foreground** *color* .br **-indicatorbackground** *color* .br **-indicatorcolor** *color* .br **-indicatormargin** *padding* .br **-indicatorrelief** *relief* .br **-padding** *padding*

Some options are only available for specific themes.

See the [ttk::style][ttk_style] manual page for information on how to configure ttk styles.


[ttk_style]: ttk_style.md
[ttk_widget]: ttk_widget.md

