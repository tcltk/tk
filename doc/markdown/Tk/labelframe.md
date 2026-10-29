---
CommandName: labelframe
title: labelframe
ManualSection: n
Version: 8.4
TclPart: Tk
TclDescription: Tk Built-In Commands
Links:
 - frame(n)
 - label(n)
 - ttk::labelframe(n)
Keywords:
 - labelframe
 - widget
Copyright:
 - Copyright (c) 1990-1994 The Regents of the University of California.
 - Copyright (c) 1994-1996 Sun Microsystems, Inc.
---

# Name

labelframe - Create and manipulate 'labelframe' labelled container widgets

# Synopsis

::: {.synopsis} :::
[labelframe]{.cmd} [pathName]{.arg} [options]{.optarg}
:::

# Standard options

::: {.standardoptions} :::
-borderwidth
-cursor
-font
-foreground
-highlightbackground
-highlightcolor
-highlightthickness
-padx
-pady
-relief
-takefocus
-text
:::

See the [options] manual entry for details on the standard options.

# Widget-specific options

[-background]{.cmdline} [background]{.dbname} [Background]{.dbclass}
: This option is the same as the standard **-background** option except that its value may also be specified as an empty string. In this case, the widget will display no background or border, and no colors will be consumed from its colormap for its background and border.

[-class]{.cmdline} [class]{.dbname} [Class]{.dbclass}
: Specifies a class for the window. This class will be used when querying the option database for the window's other options, and it will also be used later for other purposes such as bindings. The **-class** option may not be changed with the **configure** widget command.

[-colormap]{.cmdline} [colormap]{.dbname} [Colormap]{.dbclass}
: Specifies a colormap to use for the window. The value may be either **new**, in which case a new colormap is created for the window and its children, or the name of another window (which must be on the same screen and have the same visual as *pathName*), in which case the new window will use the colormap from the specified window. If the **-colormap** option is not specified, the new window uses the same colormap as its parent. This option may not be changed with the **configure** widget command.

[-height]{.cmdline} [height]{.dbname} [Height]{.dbclass}
: Specifies the desired height for the window in any of the forms acceptable to [Tk\_GetPixels][GetPixels]. If this option is negative or zero then the window will not request any size at all.

[-labelanchor]{.cmdline} [labelAnchor]{.dbname} [LabelAnchor]{.dbclass}
: Specifies where to place the label. A label is only displayed if the **-text** option is not the empty string. Valid values for this option are (listing them clockwise) **nw**, **n**, **ne**, **en**, **e**, **es**, **se**, **s**,**sw**, **ws**, **w** and **wn**. The default value is **nw**.

[-labelwidget]{.cmdline} [labelWidget]{.dbname} [LabelWidget]{.dbclass}
: Specifies a widget to use as label. This overrides any **-text** option. The widget must exist before being used as **-labelwidget** and if it is not a descendant of this window, it will be raised above it in the stacking order.

[-visual]{.cmdline} [visual]{.dbname} [Visual]{.dbclass}
: Specifies visual information for the new window in any of the forms accepted by [Tk\_GetVisual][GetVisual]. If this option is not specified, the new window will use the same visual as its parent. The **-visual** option may not be modified with the **configure** widget command.

[-width]{.cmdline} [width]{.dbname} [Width]{.dbclass}
: Specifies the desired width for the window in any of the forms acceptable to [Tk\_GetPixels][GetPixels]. If this option is negative or zero then the window will not request any size at all.


# Description

The **labelframe** command creates a new window (given by the *pathName* argument) and makes it into a labelframe widget. Additional options, described above, may be specified on the command line or in the option database to configure aspects of the labelframe such as its background color and relief.  The **labelframe** command returns the path name of the new window.

A labelframe is a simple widget.  Its primary purpose is to act as a spacer or container for complex window layouts.  It has the features of a [frame] plus the ability to display a label.

# Widget command

The **labelframe** command creates a new Tcl command whose name is the same as the path name of the labelframe's window.  This command may be used to invoke various operations on the widget.  It has the following general form:

```
pathName option ?arg ...?
```

*PathName* is the name of the command, which is the same as the labelframe widget's path name.  *Option* and the *arg*s determine the exact behavior of the command.  The following commands are possible for frame widgets:

[pathName]{.ins} [cget]{.sub} [option]{.arg}
: Returns the current value of the configuration option given by *option*. *Option* may have any of the values accepted by the **labelframe** command.

[pathName]{.ins} [configure]{.sub} [option]{.optarg} [value option value]{.optdot}
: Query or modify the configuration options of the widget. If no *option* is specified, returns a list describing all of the available options for *pathName* (see [Tk\_ConfigureInfo][ConfigWidg] for information on the format of this list).  If *option* is specified with no *value*, then the command returns a list describing the one named option (this list will be identical to the corresponding sublist of the value returned if no *option* is specified).  If one or more *option-value* pairs are specified, then the command modifies the given widget option(s) to have the given value(s);  in this case the command returns an empty string. *Option* may have any of the values accepted by the **labelframe** command.


# Bindings

When a new labelframe is created, it has no default event bindings: labelframes are not intended to be interactive.

# Example

This shows how to build part of a GUI for a hamburger vendor. The **labelframe** widgets are used to organize the available choices by the kinds of things that the choices are being made over.

```
grid [labelframe .burger -text "Burger"] \
     [labelframe .bun    -text "Bun"] -sticky news
grid [labelframe .cheese -text "Cheese Option"] \
     [labelframe .pickle -text "Pickle Option"] -sticky news
foreach {type name val} {
    burger Beef    beef
    burger Lamb    lamb
    burger Vegetarian beans

    bun    Plain   white
    bun    Sesame  seeds
    bun    Wholemeal brown

    cheese None    none
    cheese Cheddar cheddar
    cheese Edam    edam
    cheese Brie    brie
    cheese Gruy\u00e8re gruyere
    cheese "Monterey Jack" jack

    pickle None    none
    pickle Gherkins gherkins
    pickle Onions  onion
    pickle Chili   chili
} {
    set w [radiobutton .$type.$val -text $name -anchor w \
            -variable $type -value $val]
    pack $w -side top -fill x
}
set burger beef
set bun    white
set cheese none
set pickle none
```


[ConfigWidg]: ../TkCAPI/ConfigWidg.md
[frame]: frame.md
[GetPixels]: ../TkCAPI/GetPixels.md
[GetVisual]: ../TkCAPI/GetVisual.md
[options]: options.md

