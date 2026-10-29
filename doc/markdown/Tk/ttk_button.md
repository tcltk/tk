---
CommandName: ttk::button
title: ttk::button
ManualSection: n
Version: 8.5
TclPart: Tk
TclDescription: Tk Themed Widget
Links:
 - ttk::widget(n)
 - button(n)
Keywords:
 - widget
 - button
 - default
 - command
Copyright:
 - Copyright (c) 2004 Joe English
---

# Name

ttk::button - Widget that issues a command when pressed

# Synopsis

::: {.synopsis} :::
[ttk::button]{.cmd} [pathName]{.arg} [options]{.optarg}
:::

# Description

A **ttk::button** widget displays a textual label and/or image, and evaluates a command when pressed.

# Standard options

::: {.standardoptions} :::
-class
-compound
-cursor
-image
-justify
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
: A script to evaluate when the widget is invoked.

[-default]{.cmdline} [default]{.dbname} [Default]{.dbclass}
: May be set to one of  **normal**, **active**, or **disabled**. In a dialog box, one button may be designated the "default" button (meaning, roughly, "the one that gets invoked when the user presses <Enter>"). **active** indicates that this is currently the default button; **normal** means that it may become the default button, and **disabled** means that it is not defaultable. The default is **normal**.

    Depending on the theme, the default button may be displayed with an extra highlight ring, or with a different border color.


# Widget command

In addition to the standard **cget**, **configure**, **identify element**, **instate**, **state** and **style** commands (see [ttk::widget][ttk_widget]), button widgets support the following additional commands:

[pathName]{.ins} [invoke]{.sub}
: Invokes the command associated with the button.


# Standard styles

**Ttk::button** widgets support the **Toolbutton** style in all standard themes, which is useful for creating widgets for toolbars.

In the Aqua theme there are several other styles which can be used to produce replicas of many of the different button types that are discussed in Apple's Human Interface Guidelines.  These include **DisclosureButton**, **DisclosureTriangle**, **HelpButton**, **ImageButton**, **InlineButton**, **GradientButton**, **RoundedRectButton**, and **RecessedButton**.

# Styling options

The class name for a **ttk::button** is **TButton**.

Dynamic states: **active**, **disabled**, **pressed**, **readonly**.

**TButton** styling options configurable with [ttk::style][ttk_style] are:

**-anchor** *anchor* .br **-background** *color* .br **-bordercolor** *color* .br **-compound** *compound* .br **-darkcolor** *color* .br **-foreground** *color* .br **-font** *font* .br **-highlightcolor** *color* .br **-highlightthickness** *amount* .br **-justify** *justification* .br **-lightcolor** *color* .br **-padding** *padding* .br **-relief** *relief* .br **-shiftrelief** *amount* **-shiftrelief** specifies how far the button contents are shifted down and right in the *pressed* state. This action provides additional skeuomorphic feedback. **-width** *amount*

Some options are only available for specific themes.

See the [ttk::style][ttk_style] manual page for information on how to configure ttk styles.


[ttk_style]: ttk_style.md
[ttk_widget]: ttk_widget.md

