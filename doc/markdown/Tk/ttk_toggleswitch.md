---
CommandName: ttk::toggleswitch
title: ttk::toggleswitch
ManualSection: n
Version: 9.1
TclPart: Tk
TclDescription: Tk Themed Widget
Links:
 - ttk::widget(n)
 - ttk::style(n)
 - ttk::checkbutton(n)
 - checkbutton(n).
Keywords:
 - toggleswitch
 - trough
 - slider
 - widget
Copyright:
 - Copyright (c) 2025 Csaba Nemethi
---

# Name

ttk::toggleswitch - Create and manipulate a toggleswitch widget

# Synopsis

::: {.synopsis} :::
[ttk::toggleswitch]{.cmd} [pathName]{.arg} [options...]{.optarg}
:::

# Description

A **ttk::toggleswitch** widget is used to show or change a binary setting. In its simplest form it consists of a horizontal *trough* (a fully rounded filled rectangle) and a *slider* (a filled circle or fully rounded filled rectangle contained in the trough).  Their dimensions depend on the value of the **-size** configuration option, the current theme, the display's scaling level, and Tk's scaling factor given by **\[tk scaling\]** at the time the first toggleswitch widget is created.  The widget may also contain an optional text element, displayed to the trough's left.

Just like a light switch, a toggleswitch widget can have one of two possible *switch state*s: on or off.  In the on state the slider is placed at the end of the trough, and in the off state at its beginning.  The user can toggle between these two states with the mouse or the space key.

The colors used when drawing the trough and slider in the various widget states depend on the current theme.  If the theme is **aqua** then they also depend on the system appearance (light mode or dark mode) and the accent color, and are automatically adapted whenever one of these global system settings changes.

The implementation creates these elements when needed with the aid of generic code for arbitrary themes and theme-specific one for a few built-in themes. Applications can add *explicit* support for any theme *theme* by providing an appropriate command of the name **ttk::toggleswitch::CreateElements\_***theme*.

# Standard options

::: {.standardoptions} :::
-class
-cursor
-style
-takefocus
-text
-textvariable
-underline
-width
:::

See the [ttk\_widget][ttk_widget] manual entry for details on the standard options.

The default value of the **-class** option is **Toggleswitch**.  The value of the **-style** option defaults to **Toggleswitch2**, corresponding to the **-size** option's default value **2**.  Depending on the value of the **-size** option, in case of a toggleswitch without a text element the standard **-style** value is one of **Toggleswitch1**, **Toggleswitch2**, or **Toggleswitch3**, while in case of a toggleswitch containing a text element it is one of **ToggleswitchEx1**, **ToggleswitchEx2**, or **ToggleswitchEx3** (see below).

# Widget-specific options

[-command]{.cmdline} [command]{.dbname} [Command]{.dbclass}
: Specifies a Tcl script to be evaluated at global scope whenever the switch state of the widget is toggled (programmatically, by invoking the **switchstate** or **toggle** subcommand, or interactively).  The default is an empty string.

[-offvalue]{.cmdline} [offValue]{.dbname} [OffValue]{.dbclass}
: The value to store in the associated **-variable** when the widget's switch state is set to off.  Defaults to **0**.

[-onvalue]{.cmdline} [onValue]{.dbname} [OnValue]{.dbclass}
: The value to store in the associated **-variable** when the widget's switch state is set to on.  Defaults to **1**.

[-size]{.cmdline} [size]{.dbname} [Size]{.dbclass}
: Specifies the size identifier of the toggleswitch widget.  The supported values are the strings **1**, **2** (the default), and **3**.  In the case of the **aqua** theme, these size IDs correspond to the control sizes "mini", "small", and "large" of the native toggle switches on macOS.

    Note that by setting this option to a value *size*, the **-style** option's value will automatically change to **Toggleswitch***size* or **ToggleswitchEx***size* if its previous or requested value was one of **Toggleswitch1**, **Toggleswitch2**, **Toggleswitch3**, **ToggleswitchEx1**, **ToggleswitchEx2**, or **ToggleswitchEx3**. Similarly, the value of the **-style** option will automatically change to *prefix*.**Toggleswitch***size* or *prefix*.**ToggleswitchEx***size* if its previous or requested value was one of *prefix*.**Toggleswitch1**, *prefix*.**Toggleswitch2**, *prefix*.**Toggleswitch3**, *prefix*.**ToggleswitchEx1**, *prefix*.**ToggleswitchEx2**, or *prefix*.**ToggleswitchEx3**.

    Conversely, by setting the **-style** option to a value of one of the forms **Toggleswitch***size*, **ToggleswitchEx***size*, *prefix*.**Toggleswitch***size*, or *prefix*.**ToggleswitchEx***size* (where *size* is one of **1**, **2**, or **3**), the **-size** option will automatically be set to *size*.

    When configuring both options **-size** and **-style**, the former will take precedence over the latter, regardless of the order in which they were specified.  For example, if you create the widget with

    ```
    ttk::toggleswitch pathName -size 3 -style My.Toggleswitch3
    ```

    or invoke

    ```
    pathName configure -style My.Toggleswitch3
    ```

    then the **-style** option will be set to My.**Toggleswitch3** (and the **-size** option will have the value **3**).  On the other hand, if you create the widget with

    ```
    ttk::toggleswitch pathName -style My.Toggleswitch3
    ```

    then the **-style** option will have the value My.**Toggleswitch2** rather than My.**Toggleswitch3**, because the widget creation triggers the default **-size 2** setting, which takes precedence over **-style** My.**Toggleswitch3**.

[-variable]{.cmdline} [variable]{.dbname} [Variable]{.dbclass}
: The name of a global variable whose value is linked to the toggleswitch.  The widget's switch state changes to on when this variable is set to the value specified by the **-onvalue** option and to off otherwise.  Defaults to the widget's pathname if not specified.

    Note that, just like in the case of the (ttk::)checkbutton, toggling the widget's switch state by changing the value of this variable will *not* cause the script specified by the **-command** option to get executed.


# Widget command

In addition to the standard **cget**, **configure**, **identify element**, **instate**, **state**, and **style** commands (see [ttk::widget][ttk_widget]), toggleswitch widgets support the following additional commands:

[pathName]{.ins} [switchstate]{.sub} [boolean]{.optarg}
: Modifies or inquires the widget's switch state.  If the optional argument is present then it must be a boolean (a numeric value, where 0 is false and anything else is true, or a string such as **true**/**yes**/**on** or **false**/**no**/**off**).  If the widget's **disabled** state flag is set then the command returns an empty string immediately after checking the argument.  Otherwise, if the argument is true then the command sets the widget's switch state to on by setting its **selected** flag, moving the slider to the end of the trough, and setting the associated **-variable** to the value specified by the **-onvalue** option; if the argument is false then the command sets the widget's switch state to off by clearing the **selected** flag, moving the slider to the beginning of the trough, and setting the associated variable to the value specified by the **-offvalue** option.

    If the argument's value causes the widget's switch state to get toggled and the script specified as the value of the **-command** option is a nonempty string then the command evaluates that script at global scope and returns its result; otherwise the return value is an empty string.

    If the optional argument is not present then the command returns the widget's current switch state as **0** (off) or **1** (on).  When a toggleswitch widget is created, its switch state is initialized with **0**.

[pathName]{.ins} [toggle]{.sub}
: This convenience subcommand toggles the widget's switch state.  It is logically equivalent to invoking the **switchstate** command with the argument **0** if the current switch state is on and with the argument **1** otherwise.


# Internal commands

The following widget commands are used internally by the **Toggleswitch** widget class bindings.  They provide a means to access the widget's internal value, which is a real number within a certain invariant interval.

[pathName]{.ins} [get]{.sub} [min=|§max=|+x]{.optlit}
: Returns the current/minimum/maximum internal value, or the one corresponding to the coordinate *x* relative to the widget origin if it is specified.

[pathName]{.ins} [set]{.sub} [value]{.arg}
: Sets the widget's internal value to *value*.  The value will be clipped to the range given by the minimum and maximum values, as returned by **get min** and **get max**.


[pathName]{.ins} [xcoord]{.sub} [value]{.optarg}
: Returns the x coordinate corresponding to *value*, or to the current internal value if *value* is omitted.


# Default bindings

The toggleswitch widget's default bindings enable the following behavior.

If the current theme is **aqua**:

 \\(bu
: By pressing mouse button 1 over the slider and then dragging the mouse with button 1 down until the pointer enters the trough, the slider moves smoothly to the opposite edge of the trough and the widget's switch state gets toggled.  The same happens if mouse button 1 is pressed outside the slider and then the pointer leaves the widget horizontally with button 1 down.

 \\(bu
: By pressing mouse button 1 anywhere within the widget and then releasing it over the widget without previously moving the slider, the latter moves smoothly to the opposite edge of the trough and the widget's switch state gets toggled.

 \\(bu
: When the widget has the input focus, the space key causes its switch state to get toggled.


If the current theme is different from **aqua**:

 \\(bu
: By pressing mouse button 1 anywhere within the widget and then dragging the mouse with button 1 down, the slider moves in the same (horizontal) direction as the pointer.  By releasing the button, the switch state is set to off or on, depending on the slider's position relative to the middle of the widget.

 \\(bu
: By pressing mouse button 1 anywhere within the widget and then releasing it over the widget without previously dragging the mouse horizontally, the widget's switch state gets toggled.

 \\(bu
: When the widget has the input focus, the space key causes its switch state to get toggled.


If the widget's **disabled** state flag is set then none of the above actions occur.

# Widget states

The widget sets the **selected** state whenever the switch state changes to on and clears it otherwise.  The widget sets the **invalid** state whenever the linked **-variable** is unset.  The defalt bindings set and clear the **active** and **pressed** state flags.

# Styling options

The default class name for a **ttk::toggleswitch** is **Toggleswitch**.

Dynamic states: **active**, **background**, **disabled**, **invalid**, **pressed**, **selected**.

Styling options configurable with [ttk::style][ttk_style] for **Toggleswitch1**, **Toggleswitch2**, **Toggleswitch3**, **ToggleswitchEx1**, **ToggleswitchEx2**, and **ToggleswitchEx3**:

**-background** *color* .br **-focuscolor** *color* For both options, the default is theme-specific. .br **-focussolid** *boolean* Defaults to true for the **classic** theme and false for all the others. .br **-focusthickness** *amount* The default is 1.  The value may have any of the forms acceptable to [Tk\_GetPixels][GetPixels]. .br **-font** *font* For **ToggleswitchEx?** only.  The default is **TkDefaultFont**. .br **-foreground** *color* For **ToggleswitchEx?** only.  The default is theme-specific. .br **-padding** *padding* Defaults to 1.5p for the **aqua** theme (for which the three above-mentioned styles have no [focus] element) and 0.75p for all the other themes.

For the **aqua** theme the focus-related options are simply ignored.

See the [ttk\_style][ttk_style] manual page for information on how to configure ttk styles.


[focus]: focus.md
[GetPixels]: ../TkCAPI/GetPixels.md
[ttk_style]: ttk_style.md
[ttk_widget]: ttk_widget.md

