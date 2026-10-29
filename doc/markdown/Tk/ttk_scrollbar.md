---
CommandName: ttk::scrollbar
title: ttk::scrollbar
ManualSection: n
Version: 8.5
TclPart: Tk
TclDescription: Tk Themed Widget
Links:
 - ttk::widget(n)
 - scrollbar(n)
Keywords:
 - scrollbar
 - widget
Copyright:
 - Copyright (c) 1994-1996 Sun Microsystems, Inc.
 - Copyright (c) 2004 Joe English
---

# Name

ttk::scrollbar - Control the viewport of a scrollable widget

# Synopsis

::: {.synopsis} :::
[ttk::scrollbar]{.cmd} [pathName]{.arg} [options...]{.optarg}
:::

# Description

**ttk::scrollbar** widgets are typically linked to an associated window that displays a document of some sort, such as a file being edited or a drawing. A scrollbar displays a *thumb* in the middle portion of the scrollbar, whose position and size provides information about the portion of the document visible in the associated window. The thumb may be dragged by the user to control the visible region. Depending on the theme, two or more arrow buttons may also be present; these are used to scroll the visible region in discrete units.

# Standard options

::: {.standardoptions} :::
-class
-cursor
-style
-takefocus
:::

See the [ttk\_widget][ttk_widget] manual entry for details on the standard options.

# Widget-specific options

[-command]{.cmdline} [command]{.dbname} [Command]{.dbclass}
: A Tcl script prefix to evaluate to change the view in the widget associated with the scrollbar. Additional arguments are appended to the value of this option, as described in [Scrolling commands] below, whenever the user requests a view change by manipulating the scrollbar.

    This option typically consists of a two-element list, containing the name of a scrollable widget followed by either **xview** (for horizontal scrollbars) or **yview** (for vertical scrollbars).

[-orient]{.cmdline} [orient]{.dbname} [Orient]{.dbclass}
: One of **horizontal** or **vertical**. Specifies the orientation of the scrollbar.


# Widget command

In addition to the standard **cget**, **configure**, **identify element**, **instate**, **state** and **style** commands (see [ttk::widget][ttk_widget]), scrollbar widgets support the following additional commands:

[pathName]{.ins} [get]{.sub}
: Returns the scrollbar settings in the form of a list whose elements are the arguments to the most recent **set** widget command.

[pathName]{.ins} [set]{.sub} [first]{.arg} [last]{.arg}
: This command is normally invoked by the scrollbar's associated widget from an **-xscrollcommand** or **-yscrollcommand** callback. Specifies the visible range to be displayed. *first* and *last* are real fractions between 0 and 1.


# Internal commands

The following widget commands are used internally by the **TScrollbar** widget class bindings.

[pathName]{.ins} [delta]{.sub} [deltaX]{.arg} [deltaY]{.arg}
: Returns a real number indicating the fractional change in the scrollbar setting that corresponds to a given change in thumb position.  For example, if the scrollbar is horizontal, the result indicates how much the scrollbar setting must change to move the thumb *deltaX* pixels to the right (*deltaY* is ignored in this case). If the scrollbar is vertical, the result indicates how much the scrollbar setting must change to move the thumb *deltaY* pixels down.  The arguments and the result may be zero or negative.

[pathName]{.ins} [fraction]{.sub} [x]{.arg} [y]{.arg}
: Returns a real number between 0 and 1 indicating where the point given by *x* and *y* lies in the trough area of the scrollbar, where 0.0 corresponds to the top or left of the trough and 1.0 corresponds to the bottom or right. *X* and *y* are pixel coordinates relative to the scrollbar widget. If *x* and *y* refer to a point outside the trough, the closest point in the trough is used.


# Scrolling commands

When the user interacts with the scrollbar, for example by dragging the thumb, the scrollbar notifies the associated widget that it must change its view. The scrollbar makes the notification by evaluating a Tcl command generated from the scrollbar's **-command** option. The command may take any of the following forms. In each case, *prefix* is the contents of the **-command** option, which usually has a form like **.t yview**

*prefix* **moveto** *fraction*
: *Fraction* is a real number between 0 and 1. The widget should adjust its view so that the point given by *fraction* appears at the beginning of the widget. If *fraction* is 0 it refers to the beginning of the document.  1.0 refers to the end of the document, 0.333 refers to a point one-third of the way through the document, and so on.

*prefix* **scroll** *number* **pages**
: The widget should adjust its view by *number* pages. It is up to the widget to define the meaning of a page;  typically it is slightly less than what fits in the window, so that there is a slight overlap between the old and new views. *Number* is either 1, which means the next page should become visible, or -1, which means that the previous page should become visible.

*prefix* **scroll** *number* **units**
: The widget should adjust its view by *number* units. The units are defined in whatever way makes sense for the widget, such as characters or lines in a text widget. *Number* is either 1, which means one unit should scroll off the top or left of the window, or -1, which means that one unit should scroll off the bottom or right of the window.


# Widget states

The scrollbar automatically sets the **disabled** state bit. when the entire range is visible (range is 0.0 to 1.0), and clears it otherwise. It also sets the **active** and **pressed** state flags of individual elements, based on the position and state of the mouse pointer.

# Example

```
set f [frame .f]
ttk::scrollbar $f.hsb -orient horizontal -command [list $f.t xview]
ttk::scrollbar $f.vsb -orient vertical -command [list $f.t yview]
text $f.t -xscrollcommand [list $f.hsb set] -yscrollcommand [list $f.vsb set]
grid $f.t -row 0 -column 0 -sticky nsew
grid $f.vsb -row 0 -column 1 -sticky nsew
grid $f.hsb -row 1 -column 0 -sticky nsew
grid columnconfigure $f 0 -weight 1
grid rowconfigure $f 0 -weight 1
pack $f
```

# Styling options

The class name for a **ttk::scrollbar** is **TScrollbar**.

Dynamic states: **active**, **disabled**.

**TScrollbar** (or more specifically **Vertical.TScrollbar** and **Horizontal.TScrollbar**) styling options that are configurable with [ttk::style][ttk_style] are:

**-arrowcolor** *color* .br **-arrowsize** *amount* .br **-background** *color* .br **-bordercolor** *color* .br **-darkcolor** *color* (color of the dark part of the 3D relief) .br **-foreground** *color* .br **-gripsize** *size* (number of screen units) .br **-lightcolor** *color* (color of the light part of the 3D relief) .br **-troughcolor** *color*

Some options are only available for specific themes.

See the [ttk::style][ttk_style] manual page for information on how to configure ttk styles.


[ttk_style]: ttk_style.md
[ttk_widget]: ttk_widget.md

