---
CommandName: ttk::menubutton
title: ttk::menubutton
ManualSection: n
Version: 8.5
TclPart: Tk
TclDescription: Tk Themed Widget
Links:
 - ttk::widget(n)
 - menu(n)
 - menubutton(n)
Keywords:
 - widget
 - button
 - menu
Copyright:
 - Copyright (c) 2004 Joe English
---

# Name

ttk::menubutton - Widget that pops down a menu when pressed

# Synopsis

::: {.synopsis} :::
[ttk::menubutton]{.cmd} [pathName]{.arg} [options]{.optarg}
:::

# Description

A **ttk::menubutton** widget displays a textual label and/or image, and displays a menu when pressed.

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

[-direction]{.cmdline} [direction]{.dbname} [Direction]{.dbclass}
: Specifies where the menu is to be popped up relative to the menubutton. One of: **above**, **below**, **left**, **right**, or **flush**.  The default is **below**. **flush** pops the menu up directly over the menubutton.

[-menu]{.cmdline} [menu]{.dbname} [Menu]{.dbclass}
: Specifies the path name of the menu associated with the menubutton. To be on the safe side, the menu ought to be a direct child of the menubutton.


# Widget command

Menubutton widgets support the standard commands **cget**, **configure**, **identify element**, **instate**, **state** and **style** (see [ttk::widget][ttk_widget]).

# Standard styles

**Ttk::menubutton** widgets support the **Toolbutton** style in all standard themes, which is useful for creating widgets for toolbars.

# Styling options

The class name for a **ttk::menubutton** is **TMenubutton**.

Dynamic states: **active**, **disabled**, **readonly**.

**TMenubutton** styling options configurable with [ttk::style][ttk_style] are:

**-arrowsize** *amount* .br **-background** *color* .br **-compound** *compound* .br **-foreground** *color* .br **-font** *font* .br **-padding** *padding* .br **-relief** *relief* .br **-width** *amount*

Some options are only available for specific themes.

See the [ttk::style][ttk_style] manual page for information on how to configure ttk styles.


[ttk_style]: ttk_style.md
[ttk_widget]: ttk_widget.md

