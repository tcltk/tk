---
CommandName: radiobutton
title: radiobutton
ManualSection: n
Version: 4.4
TclPart: Tk
TclDescription: Tk Built-In Commands
Links:
 - checkbutton(n)
 - labelframe(n)
 - listbox(n)
 - options(n)
 - scale(n)
 - ttk::radiobutton(n)
Keywords:
 - radiobutton
 - widget
Copyright:
 - Copyright (c) 1990-1994 The Regents of the University of California.
 - Copyright (c) 1994-1996 Sun Microsystems, Inc.
---

# Name

radiobutton - Create and manipulate 'radiobutton' pick-one widgets

# Synopsis

::: {.synopsis} :::
[radiobutton]{.cmd} [pathName]{.arg} [options]{.optarg}
:::

# Standard options

::: {.standardoptions} :::
-activebackground
-activeforeground
-anchor
-background
-bitmap
-borderwidth
-compound
-cursor
-disabledforeground
-font
-foreground
-highlightbackground
-highlightcolor
-highlightthickness
-image
-justify
-padx
-pady
-relief
-takefocus
-text
-textvariable
-underline
-wraplength
:::

See the [options] manual entry for details on the standard options.

# Widget-specific options

[-command]{.cmdline} [command]{.dbname} [Command]{.dbclass}
: Specifies a Tcl command to associate with the button.  This command is typically invoked when mouse button 1 is released over the button window.  The button's global variable (**-variable** option) will be updated before the command is invoked.

[-height]{.cmdline} [height]{.dbname} [Height]{.dbclass}
: Specifies a desired height for the button. If an image or bitmap is being displayed in the button then the value is in screen units (i.e. any of the forms acceptable to [Tk\_GetPixels][GetPixels]); for text it is in lines of text. If this option is not specified, the button's desired height is computed from the size of the image or bitmap or text being displayed in it.

[-indicatoron]{.cmdline} [indicatorOn]{.dbname} [IndicatorOn]{.dbclass}
: Specifies whether or not the indicator should be drawn.  Must be a proper boolean value.  If false, the **-relief** option is ignored and the widget's relief is always sunken if the widget is selected and raised otherwise.

[-offrelief]{.cmdline} [offRelief]{.dbname} [OffRelief]{.dbclass}
: Specifies the relief for the checkbutton when the indicator is not drawn and the checkbutton is off.  The default value is "raised". By setting this option to "flat" and setting **-indicatoron** to false and **-overrelief** to "raised", the effect is achieved of having a flat button that raises on mouse-over and which is depressed when activated.  This is the behavior typically exhibited by the Align-Left, Align-Right, and Center radiobuttons on the toolbar of a word-processor, for example.

[-overrelief]{.cmdline} [overRelief]{.dbname} [OverRelief]{.dbclass}
: Specifies an alternative relief for the radiobutton, to be used when the mouse cursor is over the widget.  This option can be used to make toolbar buttons, by configuring **-relief flat -overrelief raised**.  If the value of this option is the empty string, then no alternative relief is used when the mouse cursor is over the radiobutton. The empty string is the default value.

[-selectcolor]{.cmdline} [selectColor]{.dbname} [Background]{.dbclass}
: Specifies a background color to use when the button is selected. If **indicatorOn** is true then the color is used as the background for the indicator regardless of the select state. If **-indicatoron** is false, this color is used as the background for the entire widget, in place of **-background** or **-activeBackground**, whenever the widget is selected. If specified as an empty string then no special color is used for displaying when the widget is selected.

[-selectimage]{.cmdline} [selectImage]{.dbname} [SelectImage]{.dbclass}
: Specifies an image to display (in place of the **-image** option) when the radiobutton is selected. This option is ignored unless the **-image** option has been specified.

[-state]{.cmdline} [state]{.dbname} [State]{.dbclass}
: Specifies one of three states for the radiobutton:  **normal**, **active**, or **disabled**.  In normal state the radiobutton is displayed using the **-foreground** and **-background** options.  The active state is typically used when the pointer is over the radiobutton.  In active state the radiobutton is displayed using the **-activeforeground** and **-activebackground** options.  Disabled state means that the radiobutton should be insensitive:  the default bindings will refuse to activate the widget and will ignore mouse button presses. In this state the **-disabledforeground** and **-background** options determine how the radiobutton is displayed.

[-tristateimage]{.cmdline} [tristateImage]{.dbname} [TristateImage]{.dbclass}
: Specifies an image to display (in place of the **-image** option) when the radiobutton is selected. This option is ignored unless the **-image** option has been specified.

[-tristatevalue]{.cmdline} [tristateValue]{.dbname} [Value]{.dbclass}
: Specifies the value that causes the radiobutton to display the multi-value selection, also known as the tri-state mode.  Defaults to "".

[-value]{.cmdline} [value]{.dbname} [Value]{.dbclass}
: Specifies value to store in the button's associated variable whenever this button is selected.

[-variable]{.cmdline} [variable]{.dbname} [Variable]{.dbclass}
: Specifies the name of a global variable to set whenever this button is selected.  Changes in this variable also cause the button to select or deselect itself. Defaults to the value **selectedButton**.

[-width]{.cmdline} [width]{.dbname} [Width]{.dbclass}
: Specifies a desired width for the button. If an image or bitmap is being displayed in the button, the value is in screen units (i.e. any of the forms acceptable to [Tk\_GetPixels][GetPixels]); for text it is in characters. If this option is not specified, the button's desired width is computed from the size of the image or bitmap or text being displayed in it.


# Description

The **radiobutton** command creates a new window (given by the *pathName* argument) and makes it into a radiobutton widget. Additional options, described above, may be specified on the command line or in the option database to configure aspects of the radiobutton such as its colors, font, text, and initial relief.  The **radiobutton** command returns its *pathName* argument.  At the time this command is invoked, there must not exist a window named *pathName*, but *pathName*'s parent must exist.

A radiobutton is a widget that displays a textual string, bitmap or image and a diamond or circle called an *indicator*. If text is displayed, it must all be in a single font, but it can occupy multiple lines on the screen (if it contains newlines or if wrapping occurs because of the **-wraplength** option) and one of the characters may optionally be underlined using the **-underline** option.  A radiobutton has all of the behavior of a simple button: it can display itself in either of three different ways, according to the **-state** option; it can be made to appear raised, sunken, or flat; it can be made to flash; and it invokes a Tcl command whenever mouse button 1 is clicked over the check button.

In addition, radiobuttons can be *selected*. If a radiobutton is selected, the indicator is normally drawn with a selected appearance, and a Tcl variable associated with the radiobutton is set to a particular value (normally 1). Under Unix, the indicator is drawn with a sunken relief and a special color.  Under Windows, the indicator is drawn with a round mark inside. If the radiobutton is not selected, then the indicator is drawn with a deselected appearance, and the associated variable is set to a different value (typically 0). The indicator is drawn without a round mark inside. Typically, several radiobuttons share a single variable and the value of the variable indicates which radiobutton is to be selected. When a radiobutton is selected it sets the value of the variable to indicate that fact;  each radiobutton also monitors the value of the variable and automatically selects and deselects itself when the variable's value changes. If the variable's value matches the **-tristatevalue**, then the radiobutton is drawn using the tri-state mode.  This mode is used to indicate mixed or multiple values.  (This is used when the radiobutton represents the state of multiple items.) By default the variable **selectedButton** is used;  its contents give the name of the button that is selected, or the empty string if no button associated with that variable is selected. The name of the variable for a radiobutton, plus the variable to be stored into it, may be modified with options on the command line or in the option database. Configuration options may also be used to modify the way the indicator is displayed (or whether it is displayed at all). By default a radiobutton is configured to select itself on button clicks.

# Widget command

The **radiobutton** command creates a new Tcl command whose name is *pathName*.  This command may be used to invoke various operations on the widget.  It has the following general form:

```
pathName option ?arg ...?
```

*Option* and the *arg*s determine the exact behavior of the command.  The following commands are possible for radiobutton widgets:

[pathName]{.ins} [cget]{.sub} [option]{.arg}
: Returns the current value of the configuration option given by *option*. *Option* may have any of the values accepted by the **radiobutton** command.

[pathName]{.ins} [configure]{.sub} [option]{.optarg} [value option value]{.optdot}
: Query or modify the configuration options of the widget. If no *option* is specified, returns a list describing all of the available options for *pathName* (see [Tk\_ConfigureInfo][ConfigWidg] for information on the format of this list).  If *option* is specified with no *value*, the command returns a list describing the one named option (this list will be identical to the corresponding sublist of the value returned if no *option* is specified).  If one or more *option-value* pairs are specified, the command modifies the given widget option(s) to have the given value(s);  in this case the command returns an empty string. *Option* may have any of the values accepted by the **radiobutton** command.

[pathName]{.ins} [deselect]{.sub}
: Deselects the radiobutton and sets the associated variable to an empty string. If this radiobutton was not currently selected, the command has no effect.

[pathName]{.ins} [flash]{.sub}
: Flashes the radiobutton.  This is accomplished by redisplaying the radiobutton several times, alternating between active and normal colors.  At the end of the flash the radiobutton is left in the same normal/active state as when the command was invoked. This command is ignored if the radiobutton's state is **disabled**.

[pathName]{.ins} [invoke]{.sub}
: Does just what would have happened if the user invoked the radiobutton with the mouse: selects the button and invokes its associated Tcl command, if there is one. The return value is the return value from the Tcl command, or an empty string if there is no command associated with the radiobutton. This command is ignored if the radiobutton's state is **disabled**.

[pathName]{.ins} [select]{.sub}
: Selects the radiobutton and sets the associated variable to the value corresponding to this widget.


# Bindings

Tk automatically creates class bindings for radiobuttons that give them the following default behavior:

1. On Unix systems, a radiobutton activates whenever the mouse passes over it and deactivates whenever the mouse leaves the radiobutton.  On Mac and Windows systems, when mouse button 1 is pressed over a radiobutton, the button activates whenever the mouse pointer is inside the button, and deactivates whenever the mouse pointer leaves the button.

2. When mouse button 1 is pressed over a radiobutton it is invoked (it becomes selected and the command associated with the button is invoked, if there is one).

3. When a radiobutton has the input focus, the space key causes the radiobutton to be invoked.


If the radiobutton's state is **disabled** then none of the above actions occur:  the radiobutton is completely non-responsive.

The behavior of radiobuttons can be changed by defining new bindings for individual widgets or by redefining the class bindings.


[ConfigWidg]: ../TkCAPI/ConfigWidg.md
[GetPixels]: ../TkCAPI/GetPixels.md
[options]: options.md

