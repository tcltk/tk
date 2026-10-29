---
CommandName: ttk::separator
title: ttk::separator
ManualSection: n
Version: 8.5
TclPart: Tk
TclDescription: Tk Themed Widget
Links:
 - ttk::widget(n)
Keywords:
 - widget
 - separator
Copyright:
 - Copyright (c) 2004 Joe English
---

# Name

ttk::separator - Separator bar

# Synopsis

::: {.synopsis} :::
[ttk::separator]{.cmd} [pathName]{.arg} [options]{.optarg}
:::

# Description

A **ttk::separator** widget displays a horizontal or vertical separator bar.

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
: One of **horizontal** or **vertical**. Specifies the orientation of the separator.


# Widget command

Separator widgets support the standard commands **cget**, **configure**, **identify element**, **instate**, **state** and **style** (see [ttk::widget][ttk_widget]).

# Styling options

The class name for a **ttk::separator** is **TSeparator**.

**TSeparator** styling options configurable with [ttk::style][ttk_style] are:

**-background** *color*

Some options are only available for specific themes.

See the [ttk::style][ttk_style] manual page for information on how to configure ttk styles.


[ttk_style]: ttk_style.md
[ttk_widget]: ttk_widget.md

