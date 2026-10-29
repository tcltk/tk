---
CommandName: scrollbar
title: scrollbar
ManualSection: n
Version: 4.1
TclPart: Tk
TclDescription: Tk Built-In Commands
Links:
 - ttk::scrollbar(n)
Keywords:
 - scrollbar
 - widget
Copyright:
 - Copyright (c) 1990-1994 The Regents of the University of California.
 - Copyright (c) 1994-1996 Sun Microsystems, Inc.
---

# Name

scrollbar - Create and manipulate 'scrollbar' scrolling control and indicator widgets

# Synopsis

::: {.synopsis} :::
[scrollbar]{.cmd} [pathName]{.arg} [options]{.optarg}
:::

# Standard options

::: {.standardoptions} :::
-activebackground
-background
-borderwidth
-cursor
-highlightbackground
-highlightcolor
-highlightthickness
-jump
-orient
-relief
-repeatdelay
-repeatinterval
-takefocus
-troughcolor
:::

See the [options] manual entry for details on the standard options.

# Widget-specific options

[-activerelief]{.cmdline} [activeRelief]{.dbname} [ActiveRelief]{.dbclass}
: Specifies the relief to use when displaying the element that is active, if any. Elements other than the active element are always displayed with a raised relief.

[-command]{.cmdline} [command]{.dbname} [Command]{.dbclass}
: Specifies the prefix of a Tcl command to invoke to change the view in the widget associated with the scrollbar.  When a user requests a view change by manipulating the scrollbar, a Tcl command is invoked.  The actual command consists of this option followed by additional information as described later.  This option almost always has a value such as **.t xview** or **.t yview**, consisting of the name of a widget and either **xview** (if the scrollbar is for horizontal scrolling) or **yview** (for vertical scrolling). All scrollable widgets have **xview** and **yview** commands that take exactly the additional arguments appended by the scrollbar as described in [Scrolling commands] below.

[-elementborderwidth]{.cmdline} [elementBorderWidth]{.dbname} [BorderWidth]{.dbclass}
: Specifies the width of borders drawn around the internal elements of the scrollbar (the two arrows and the slider).  The value may have any of the forms acceptable to [Tk\_GetPixels][GetPixels]. If this value is the empty string (the default), the value of the **-borderwidth** option is used in its place.

[-width]{.cmdline} [width]{.dbname} [Width]{.dbclass}
: Specifies the desired narrow dimension of the scrollbar window, not including 3-D border, if any.  For vertical scrollbars this will be the width and for horizontal scrollbars this will be the height. The value may have any of the forms acceptable to [Tk\_GetPixels][GetPixels].


# Description

The **scrollbar** command creates a new window (given by the *pathName* argument) and makes it into a scrollbar widget. Additional options, described above, may be specified on the command line or in the option database to configure aspects of the scrollbar such as its colors, orientation, and relief. The **scrollbar** command returns its *pathName* argument. At the time this command is invoked, there must not exist a window named *pathName*, but *pathName*'s parent must exist.

A scrollbar is a widget that displays two arrows, one at each end of the scrollbar, and a *slider* in the middle portion of the scrollbar. It provides information about what is visible in an *associated window* that displays a document of some sort (such as a file being edited or a drawing). The position and size of the slider indicate which portion of the document is visible in the associated window.  For example, if the slider in a vertical scrollbar covers the top third of the area between the two arrows, it means that the associated window displays the top third of its document.

Scrollbars can be used to adjust the view in the associated window by clicking or dragging with the mouse.  See the [Bindings] section below for details.

# Elements

A scrollbar displays five elements, which are referred to in the widget commands for the scrollbar:

**arrow1**
: The top or left arrow in the scrollbar.

**trough1**
: The region between the slider and **arrow1**.

**slider**
: The rectangle that indicates what is visible in the associated widget.

**trough2**
: The region between the slider and **arrow2**.

**arrow2**
: The bottom or right arrow in the scrollbar.


# Widget command

The **scrollbar** command creates a new Tcl command whose name is *pathName*.  This command may be used to invoke various operations on the widget.  It has the following general form:

```
pathName option ?arg ...?
```

*Option* and the *arg*s determine the exact behavior of the command.  The following commands are possible for scrollbar widgets:

[pathName]{.ins} [activate]{.sub} [element]{.optarg}
: Marks the element indicated by *element* as active, which causes it to be displayed as specified by the **-activebackground** and **-activerelief** options. The only element values understood by this command are **arrow1**, **slider**, or **arrow2**. If any other value is specified then no element of the scrollbar will be active. If *element* is not specified, the command returns the name of the element that is currently active, or an empty string if no element is active.

[pathName]{.ins} [cget]{.sub} [option]{.arg}
: Returns the current value of the configuration option given by *option*. *Option* may have any of the values accepted by the **scrollbar** command.

[pathName]{.ins} [configure]{.sub} [option]{.optarg} [value option value]{.optdot}
: Query or modify the configuration options of the widget. If no *option* is specified, returns a list describing all of the available options for *pathName* (see [Tk\_ConfigureInfo][ConfigWidg] for information on the format of this list).  If *option* is specified with no *value*, then the command returns a list describing the one named option (this list will be identical to the corresponding sublist of the value returned if no *option* is specified).  If one or more *option-value* pairs are specified, then the command modifies the given widget option(s) to have the given value(s);  in this case the command returns an empty string. *Option* may have any of the values accepted by the **scrollbar** command.

[pathName]{.ins} [delta]{.sub} [deltaX]{.arg} [deltaY]{.arg}
: Returns a real number indicating the fractional change in the scrollbar setting that corresponds to a given change in slider position.  For example, if the scrollbar is horizontal, the result indicates how much the scrollbar setting must change to move the slider *deltaX* pixels to the right (*deltaY* is ignored in this case). If the scrollbar is vertical, the result indicates how much the scrollbar setting must change to move the slider *deltaY* pixels down.  The arguments and the result may be zero or negative.

[pathName]{.ins} [fraction]{.sub} [x]{.arg} [y]{.arg}
: Returns a real number between 0 and 1 indicating where the point given by *x* and *y* lies in the trough area of the scrollbar. The value 0 corresponds to the top or left of the trough, the value 1 corresponds to the bottom or right, 0.5 corresponds to the middle, and so on. *X* and *y* must be pixel coordinates relative to the scrollbar widget. If *x* and *y* refer to a point outside the trough, the closest point in the trough is used.

[pathName]{.ins} [get]{.sub}
: Returns the scrollbar settings in the form of a list whose elements are the arguments to the most recent **set** widget command.

[pathName]{.ins} [identify]{.sub} [x]{.arg} [y]{.arg}
: Returns the name of the element under the point given by *x* and *y* (such as **arrow1**), or an empty string if the point does not lie in any element of the scrollbar. *X* and *y* must be pixel coordinates relative to the scrollbar widget.

[pathName]{.ins} [set]{.sub} [first]{.arg} [last]{.arg}
: This command is invoked by the scrollbar's associated widget to tell the scrollbar about the current view in the widget. The command takes two arguments, each of which is a real fraction between 0 and 1. The fractions describe the range of the document that is visible in the associated widget. For example, if *first* is 0.2 and *last* is 0.4, it means that the first part of the document visible in the window is 20% of the way through the document, and the last visible part is 40% of the way through.


# Scrolling commands

When the user interacts with the scrollbar, for example by dragging the slider, the scrollbar notifies the associated widget that it must change its view. The scrollbar makes the notification by evaluating a Tcl command generated from the scrollbar's **-command** option. The command may take any of the following forms. In each case, *prefix* is the contents of the **-command** option, which usually has a form like "**.t yview**".

[prefix]{.ins} [moveto]{.sub} [fraction]{.arg}
: *Fraction* is a real number between 0 and 1. The widget should adjust its view so that the point given by *fraction* appears at the beginning of the widget. If *fraction* is 0 it refers to the beginning of the document.  1.0 refers to the end of the document, 0.333 refers to a point one-third of the way through the document, and so on.

[prefix]{.ins} [scroll]{.sub} [number]{.arg} [pages]{.lit}
: The widget should adjust its view by *number* pages. It is up to the widget to define the meaning of a page;  typically it is slightly less than what fits in the window, so that there is a slight overlap between the old and new views. *Number* is either 1, which means the next page should become visible, or -1, which means that the previous page should become visible. Fractional numbers are rounded away from 0, so scrolling 0.001 pages has the same effect as scrolling 1 page.

*prefix* **scroll** *number* **units**
: The widget should adjust its view by *number* units. The units are defined in whatever way makes sense for the widget, such as characters or lines in a text widget. *Number* is either 1, which means one unit should scroll off the top or left of the window, or -1, which means that one unit should scroll off the bottom or right of the window. Fractional numbers are rounded away from 0, so scrolling 0.001 units has the same effect as scrolling 1 unit.


# Bindings

Tk automatically creates class bindings for scrollbars that give them the following default behavior. If the behavior is different for vertical and horizontal scrollbars, the horizontal behavior is described in parentheses.

1. Pressing button 1 over **arrow1** causes the view in the associated widget to shift up (left) by one unit so that the document appears to move down (right) one unit. If the button is held down, the action auto-repeats.

2. Pressing button 1 over **trough1** causes the view in the associated widget to shift up (left) by one screenful so that the document appears to move down (right) one screenful. If the button is held down, the action auto-repeats.

3. Pressing button 1 over the slider and dragging causes the view to drag with the slider. If the **jump** option is true, then the view does not drag along with the slider;  it changes only when the mouse button is released.

4. Pressing button 1 over **trough2** causes the view in the associated widget to shift down (right) by one screenful so that the document appears to move up (left) one screenful. If the button is held down, the action auto-repeats.

5. Pressing button 1 over **arrow2** causes the view in the associated widget to shift down (right) by one unit so that the document appears to move up (left) one unit. If the button is held down, the action auto-repeats.

6. If button 2 is pressed over the trough or the slider, it sets the view to correspond to the mouse position;  dragging the mouse with button 2 down causes the view to drag with the mouse. If button 2 is pressed over one of the arrows, it causes the same behavior as pressing button 1.

7. If button 1 is pressed with the Control key down, then if the mouse is over **arrow1** or **trough1** the view changes to the very top (left) of the document;  if the mouse is over **arrow2** or **trough2** the view changes to the very bottom (right) of the document;  if the mouse is anywhere else then the button press has no effect.

8. In vertical scrollbars the Up and Down keys have the same behavior as mouse clicks over **arrow1** and **arrow2**, respectively. In horizontal scrollbars these keys have no effect.

9. In vertical scrollbars Control-Up and Control-Down have the same behavior as mouse clicks over **trough1** and **trough2**, respectively. In horizontal scrollbars these keys have no effect.

10. In horizontal scrollbars the Up and Down keys have the same behavior as mouse clicks over **arrow1** and **arrow2**, respectively. In vertical scrollbars these keys have no effect.

11. In horizontal scrollbars Control-Up and Control-Down have the same behavior as mouse clicks over **trough1** and **trough2**, respectively. In vertical scrollbars these keys have no effect.

12. The Prior and Next keys have the same behavior as mouse clicks over **trough1** and **trough2**, respectively.

13. The Home key adjusts the view to the top (left edge) of the document.

14. The End key adjusts the view to the bottom (right edge) of the document.


# Example

Create a window with a scrollable [text] widget:

```
toplevel .tl
text .tl.t -yscrollcommand {.tl.s set}
scrollbar .tl.s -command {.tl.t yview}
grid .tl.t .tl.s -sticky nsew
grid columnconfigure .tl 0 -weight 1
grid rowconfigure .tl 0 -weight 1
```


[ConfigWidg]: ../TkCAPI/ConfigWidg.md
[GetPixels]: ../TkCAPI/GetPixels.md
[options]: options.md
[text]: text.md

