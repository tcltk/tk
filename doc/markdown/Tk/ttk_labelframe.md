---
CommandName: ttk::labelframe
title: ttk::labelframe
ManualSection: n
Version: 8.5
TclPart: Tk
TclDescription: Tk Themed Widget
Links:
 - ttk::widget(n)
 - ttk::frame(n)
 - labelframe(n)
Keywords:
 - widget
 - frame
 - container
 - label
 - groupbox
Copyright:
 - Copyright (c) 2005 Joe English
---

# Name

ttk::labelframe - Container widget with optional label

# Synopsis

::: {.synopsis} :::
[ttk::labelframe]{.cmd} [pathName]{.arg} [options]{.optarg}
:::

# Description

A **ttk::labelframe** widget is a container used to group other widgets together.  It has an optional label, which may be a plain text string or another widget.

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

[-height]{.cmdline} [height]{.dbname} [Height]{.dbclass}
: If specified, the widget's requested height in pixels. (See *ttk::frame(n)* for further notes on **-width** and **-height**).

[-labelanchor]{.cmdline} [labelAnchor]{.dbname} [LabelAnchor]{.dbclass}
: Specifies where to place the label. Allowed values are (clockwise from the top upper left corner): **nw**, **n**, **ne**, **en**, **e**, **es**, **se**, **s**,**sw**, **ws**, **w** and **wn**. The default value is theme-dependent.

[-labelwidget]{.cmdline} [labelWidget]{.dbname} [LabelWidget]{.dbclass}
: The name of a widget to use for the label. If set, overrides the **-text** option. The **-labelwidget** must be a child of the [labelframe] widget or one of the [labelframe]'s ancestors, and must belong to the same top-level widget as the [labelframe].

[-text]{.cmdline} [text]{.dbname} [Text]{.dbclass}
: Specifies the text of the label.

[-underline]{.cmdline} [underline]{.dbname} [Underline]{.dbclass}
: If set, specifies the integer index (0-based) of a character to underline in the text string. The underlined character is used for mnemonic activation. Mnemonic activation for a **ttk::labelframe** sets the keyboard focus to the first child of the **ttk::labelframe** widget.

[-width]{.cmdline} [width]{.dbname} [Width]{.dbclass}
: If specified, the widget's requested width in pixels.


# Widget command

Labelframe widgets support the standard commands **cget**, **configure**, **identify element**, **instate**, **state** and **style** (see [ttk::widget][ttk_widget]).

# Styling options

The class name for a **ttk::labelframe** is **TLabelframe**. The text label has a class of **TLabelframe.Label**.

Dynamic states: **disabled**, **readonly**.

**TLabelframe** styling options configurable with [ttk::style][ttk_style] are:

**-background** *color* .br **-bordercolor** *color* .br **-borderwidth** *amount* .br **-darkcolor** *color* .br **-labelmargins** *amount* .br **-labeloutside** *boolean* .br **-lightcolor** *color* .br **-relief** *relief*

**TLabelframe.Label** styling options configurable with [ttk::style][ttk_style] are:

**-background** *color* .br **-font** *font* .br **-foreground** *color*

Some options are only available for specific themes.

See the [ttk::style][ttk_style] manual page for information on how to configure ttk styles.


[labelframe]: labelframe.md
[ttk_style]: ttk_style.md
[ttk_widget]: ttk_widget.md

