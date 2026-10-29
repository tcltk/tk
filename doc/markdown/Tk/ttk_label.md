---
CommandName: ttk::label
title: ttk::label
ManualSection: n
Version: 8.5
TclPart: Tk
TclDescription: Tk Themed Widget
Links:
 - ttk::widget(n)
 - label(n)
Copyright:
 - Copyright (c) 2004 Joe English
---

# Name

ttk::label - Display a text string and/or image

# Synopsis

::: {.synopsis} :::
[ttk::label]{.cmd} [pathName]{.arg} [options]{.optarg}
:::

# Description

A **ttk::label** widget displays a textual label and/or image. The label may be linked to a Tcl variable to automatically change the displayed text.

# Standard options

::: {.standardoptions} :::
-anchor
-class
-compound
-cursor
-font
-foreground
-image
-justify
-padding
-state
-style
-takefocus
-text
-textangle
-textvariable
-underline
-width
-wraplength
:::

See the [ttk\_widget][ttk_widget] manual entry for details on the standard options.

# Widget-specific options

[-background]{.cmdline} [frameColor]{.dbname} [FrameColor]{.dbclass}
: The widget's background color. If unspecified, the theme default is used.

[-relief]{.cmdline} [relief]{.dbname} [Relief]{.dbclass}
: Specifies the 3-D effect desired for the widget border. Valid values are **flat**, **groove**, **raised**, **ridge**, **solid**, and **sunken**.

[-wraplength]{.cmdline} [wrapLength]{.dbname} [WrapLength]{.dbclass}
: Specifies the maximum line length (in pixels). If this option is negative or zero, then automatic wrapping is not performed; otherwise the text is split into lines such that no line is longer than the specified value.


# Widget command

Label widgets support the standard commands **cget**, **configure**, **identify element**, **instate**, **state** and **style** (see [ttk::widget][ttk_widget]).

# Styling options

The class name for a **ttk::label** is **TLabel**.

Dynamic states: **disabled**, **readonly**.

**TLabel** styling options configurable with [ttk::style][ttk_style] are:

**-background** *color* .br **-compound** *compound* .br **-foreground** *color* .br **-font** *font*

Some options are only available for specific themes.

See the [ttk::style][ttk_style] manual page for information on how to configure ttk styles.


[ttk_style]: ttk_style.md
[ttk_widget]: ttk_widget.md

