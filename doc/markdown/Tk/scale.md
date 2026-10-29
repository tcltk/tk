---
CommandName: scale
title: scale
ManualSection: n
Version: 4.1
TclPart: Tk
TclDescription: Tk Built-In Commands
Links:
 - ttk::scale(n)
Keywords:
 - scale
 - slider
 - trough
 - widget
Copyright:
 - Copyright (c) 1990-1994 The Regents of the University of California.
 - Copyright (c) 1994-1996 Sun Microsystems, Inc.
---

# Name

scale - Create and manipulate 'scale' value-controlled slider widgets

# Synopsis

::: {.synopsis} :::
[scale]{.cmd} [pathName]{.arg} [options]{.optarg}
:::

# Standard options

::: {.standardoptions} :::
-activebackground
-background
-borderwidth
-cursor
-font
-foreground
-highlightbackground
-highlightcolor
-highlightthickness
-orient
-relief
-repeatdelay
-repeatinterval
-takefocus
-troughcolor
:::

See the [options] manual entry for details on the standard options.

# Widget-specific options

[-bigincrement]{.cmdline} [bigIncrement]{.dbname} [BigIncrement]{.dbclass}
: Some interactions with the scale cause its value to change by "large" increments;  this option specifies the size of the large increments.  If specified as 0, the large increments default to 1/10 the range of the scale.

[-command]{.cmdline} [command]{.dbname} [Command]{.dbclass}
: Specifies the prefix of a Tcl command to invoke whenever the scale's value is changed via a widget command. The actual command consists of this option followed by a space and a real number indicating the new value of the scale.

[-digits]{.cmdline} [digits]{.dbname} [Digits]{.dbclass}
: An integer specifying how many significant digits should be retained when converting the value of the scale to a string. If the number is negative or zero, then the scale picks the smallest value that guarantees that every possible slider position prints as a different string.

[-from]{.cmdline} [from]{.dbname} [From]{.dbclass}
: A real value corresponding to the left or top end of the scale.

[-label]{.cmdline} [label]{.dbname} [Label]{.dbclass}
: A string to display as a label for the scale.  For vertical scales the label is displayed just to the right of the top end of the scale.  For horizontal scales the label is displayed just above the left end of the scale.  If the option is specified as an empty string, no label is displayed.

[-length]{.cmdline} [length]{.dbname} [Length]{.dbclass}
: Specifies the desired long dimension of the scale in screen units (i.e. any of the forms acceptable to [Tk\_GetPixels][GetPixels]). For vertical scales this is the scale's height;  for horizontal scales it is the scale's width.

[-resolution]{.cmdline} [resolution]{.dbname} [Resolution]{.dbclass}
: A real value specifying the resolution for the scale. If this value is greater than zero then the scale's value will always be rounded to an even multiple of this value, as will the endpoints of the scale.  If the value is negative then no rounding occurs.  Defaults to 1 (i.e., the value will be integral).

[-showvalue]{.cmdline} [showValue]{.dbname} [ShowValue]{.dbclass}
: Specifies a boolean value indicating whether or not the current value of the scale is to be displayed.

[-sliderlength]{.cmdline} [sliderLength]{.dbname} [SliderLength]{.dbclass}
: Specifies the size of the slider, measured in screen units along the slider's long dimension.  The value may be specified in any of the forms acceptable to [Tk\_GetPixels][GetPixels].

[-sliderrelief]{.cmdline} [sliderRelief]{.dbname} [SliderRelief]{.dbclass}
: Specifies the relief to use when drawing the slider, such as **raised** or **sunken**.

[-state]{.cmdline} [state]{.dbname} [State]{.dbclass}
: Specifies one of three states for the scale:  **normal**, **active**, or **disabled**. If the scale is disabled then the value may not be changed and the scale will not activate. If the scale is active, the slider is displayed using the color specified by the **-activebackground** option.

[-tickinterval]{.cmdline} [tickInterval]{.dbname} [TickInterval]{.dbclass}
: Must be a real value. Determines the spacing between numerical tick marks displayed below or to the left of the slider. The values will all be displayed with the same number of decimal places, which will be enough to ensure they are all accurate to within 20% of a tick interval. If 0, no tick marks will be displayed.

[-to]{.cmdline} [to]{.dbname} [To]{.dbclass}
: Specifies a real value corresponding to the right or bottom end of the scale. This value may be either less than or greater than the **-from** option.

[-variable]{.cmdline} [variable]{.dbname} [Variable]{.dbclass}
: Specifies the name of a global variable to link to the scale.  Whenever the value of the variable changes, the scale will update to reflect this value. Whenever the scale is manipulated interactively, the variable will be modified to reflect the scale's new value.

[-width]{.cmdline} [width]{.dbname} [Width]{.dbclass}
: Specifies the desired narrow dimension of the scale in screen units (i.e. any of the forms acceptable to [Tk\_GetPixels][GetPixels]). For vertical scales this is the scale's width;  for horizontal scales this is the scale's height.


# Description

The **scale** command creates a new window (given by the *pathName* argument) and makes it into a scale widget. Additional options, described above, may be specified on the command line or in the option database to configure aspects of the scale such as its colors, orientation, and relief.  The **scale** command returns its *pathName* argument.  At the time this command is invoked, there must not exist a window named *pathName*, but *pathName*'s parent must exist.

A scale is a widget that displays a rectangular *trough* and a small *slider*.  The trough corresponds to a range of real values (determined by the **-from**, **-to**, and **-resolution** options), and the position of the slider selects a particular real value. The slider's position (and hence the scale's value) may be adjusted with the mouse or keyboard as described in the [Bindings] section below.  Whenever the scale's value is changed, a Tcl command is invoked (using the **-command** option) to notify other interested widgets of the change. In addition, the value of the scale can be linked to a Tcl variable (using the **-variable** option), so that changes in either are reflected in the other.

Three annotations may be displayed in a scale widget:  a label appearing at the top right of the widget (top left for horizontal scales), a number displayed just to the left of the slider (just above the slider for horizontal scales), and a collection of numerical tick marks just to the left of the current value (just below the trough for horizontal scales).  Each of these three annotations may be enabled or disabled using the configuration options.

# Widget command

The **scale** command creates a new Tcl command whose name is *pathName*.  This command may be used to invoke various operations on the widget.  It has the following general form:

```
pathName option ?arg ...?
```

*Option* and the *arg*s determine the exact behavior of the command.  The following commands are possible for scale widgets:

[pathName]{.ins} [cget]{.sub} [option]{.arg}
: Returns the current value of the configuration option given by *option*. *Option* may have any of the values accepted by the **scale** command.

[pathName]{.ins} [configure]{.sub} [option]{.optarg} [value option value]{.optdot}
: Query or modify the configuration options of the widget. If no *option* is specified, returns a list describing all of the available options for *pathName* (see [Tk\_ConfigureInfo][ConfigWidg] for information on the format of this list).  If *option* is specified with no *value*, then the command returns a list describing the one named option (this list will be identical to the corresponding sublist of the value returned if no *option* is specified).  If one or more *option-value* pairs are specified, then the command modifies the given widget option(s) to have the given value(s);  in this case the command returns an empty string. *Option* may have any of the values accepted by the **scale** command.

[pathName]{.ins} [coords]{.sub} [value]{.optarg}
: Returns a list whose elements are the x and y coordinates of the point along the centerline of the trough that corresponds to *value*. If *value* is omitted then the scale's current value is used.

[pathName]{.ins} [get]{.sub} [x y]{.optarg}
: If *x* and *y* are omitted, returns the current value of the scale.  If *x* and *y* are specified, they give pixel coordinates within the widget;  the command returns the scale value corresponding to the given pixel. Only one of *x* or *y* is used:  for horizontal scales *y* is ignored, and for vertical scales *x* is ignored.

[pathName]{.ins} [identify]{.sub} [x]{.arg} [y]{.arg}
: Returns a string indicating what part of the scale lies under the coordinates given by *x* and *y*. A return value of **slider** means that the point is over the slider;  **trough1** means that the point is over the portion of the slider above  or to the left of the slider; and **trough2** means that the point is over the portion of the slider below or to the right of the slider. If the point is not over one of these elements, an empty string is returned.

[pathName]{.ins} [set]{.sub} [value]{.arg}
: This command is invoked to change the current value of the scale, and hence the position at which the slider is displayed.  *Value* gives the new value for the scale. The command has no effect if the scale is disabled.


# Bindings

Tk automatically creates class bindings for scales that give them the following default behavior. Where the behavior is different for vertical and horizontal scales, the horizontal behavior is described in parentheses.

1. If button 1 is pressed in the trough, the scale's value will be incremented or decremented by the value of the **-resolution** option so that the slider moves in the direction of the cursor. If the button is held down, the action auto-repeats.

2. If button 1 is pressed over the slider, the slider can be dragged with the mouse.

3. If button 1 is pressed in the trough with the Control key down, the slider moves all the way to the end of its range, in the direction towards the mouse cursor.

4. If button 2 is pressed, the scale's value is set to the mouse position.  If the mouse is dragged with button 2 down, the scale's value changes with the drag.

5. The Up and Left keys move the slider up (left) by the value of the **-resolution** option.

6. The Down and Right keys move the slider down (right) by the value of the **-resolution** option.

7. Control-Up and Control-Left move the slider up (left) by the value of the **-bigincrement** option.

8. Control-Down and Control-Right move the slider down (right) by the value of the **-bigincrement** option.

9. Home moves the slider to the top (left) end of its range.

10. End moves the slider to the bottom (right) end of its range.


If the scale is disabled using the **-state** option then none of the above bindings have any effect.

The behavior of scales can be changed by defining new bindings for individual widgets or by redefining the class bindings.


[ConfigWidg]: ../TkCAPI/ConfigWidg.md
[GetPixels]: ../TkCAPI/GetPixels.md
[options]: options.md

