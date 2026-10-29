---
CommandName: ttk::frame
title: ttk::frame
ManualSection: n
Version: 8.5
TclPart: Tk
TclDescription: Tk Themed Widget
Links:
 - ttk::widget(n)
 - ttk::labelframe(n)
 - frame(n)
Keywords:
 - widget
 - frame
 - container
Copyright:
 - Copyright (c) 2005 Joe English
---

# Name

ttk::frame - Simple container widget

# Synopsis

::: {.synopsis} :::
[ttk::frame]{.cmd} [pathName]{.arg} [options]{.optarg}
:::

# Description

A **ttk::frame** widget is a container, used to group other widgets together.

# Standard options

::: {.standardoptions} :::
-class
-cursor
-padding
-style
-takefocus
:::

See the [ttk\_widget][ttk_widget] manual entry for details on the standard options.

# Widget-specific options

[-borderwidth]{.cmdline} [borderWidth]{.dbname} [BorderWidth]{.dbclass}
: The desired width of the widget border.  Defaults to 0. May be ignored depending on the theme used.

[-relief]{.cmdline} [relief]{.dbname} [Relief]{.dbclass}
: One of the standard Tk border styles: **flat**, **groove**, **raised**, **ridge**, **solid**, or **sunken**. Defaults to **flat**.

[-width]{.cmdline} [width]{.dbname} [Width]{.dbclass}
: If specified, the widget's requested width in pixels.

[-height]{.cmdline} [height]{.dbname} [Height]{.dbclass}
: If specified, the widget's requested height in pixels.


# Widget command

Frame widgets support the standard commands **cget**, **configure**, **identify element**, **instate**, **state** and **style** (see [ttk::widget][ttk_widget]).

# Notes

Note that if the [pack], [grid], or other geometry managers are used to manage the children of the [frame], by the GM's requested size will normally take precedence over the [frame] widget's **-width** and **-height** options. [pack propagate][pack] and [grid propagate][grid] can be used to change this.

# Styling options

The class name for a **ttk::frame** is **TFrame**.

**TFrame** styling options configurable with [ttk::style][ttk_style] are:

**-background** *color* .br **-relief** *relief*

Some options are only available for specific themes.

See the [ttk::style][ttk_style] manual page for information on how to configure ttk styles.

# Bindings

When a new **ttk::frame** is created, it has no default event bindings; **ttk::frame**s are not intended to be interactive.


[frame]: frame.md
[grid]: grid.md
[pack]: pack.md
[ttk_style]: ttk_style.md
[ttk_widget]: ttk_widget.md

