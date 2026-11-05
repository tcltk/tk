---
CommandName: panedwindow
title: panedwindow
ManualSection: n
Version: 8.4
TclPart: Tk
TclDescription: Tk Built-In Commands
Links:
 - ttk::panedwindow(n)
Keywords:
 - panedwindow
 - widget
 - geometry management
Copyright:
 - Copyright (c) 1992 The Regents of the University of California.
 - Copyright (c) 1994-1996 Sun Microsystems, Inc.
---

# Name

panedwindow - Create and manipulate 'panedwindow' split container widgets

# Synopsis

::: {.synopsis} :::
[panedwindow]{.cmd} [pathName]{.arg} [options]{.optarg}
:::

# Standard options

::: {.standardoptions} :::
-background
-borderwidth
-cursor
-orient
-relief
:::

See the [options] manual entry for details on the standard options.

# Widget-specific options

[-handlepad]{.cmdline} [handlePad]{.dbname} [HandlePad]{.dbclass}
: When sash handles are drawn, specifies the distance from the top or left end of the sash (depending on the orientation of the widget) at which to draw the handle.  May be any value accepted by [Tk\_GetPixels][GetPixels].

[-handlesize]{.cmdline} [handleSize]{.dbname} [HandleSize]{.dbclass}
: Specifies the side length of a sash handle.  Handles are always drawn as squares.  May be any value accepted by [Tk\_GetPixels][GetPixels].

[-height]{.cmdline} [height]{.dbname} [Height]{.dbclass}
: Specifies a desired height for the overall panedwindow widget. May be any value accepted by [Tk\_GetPixels][GetPixels]. If an empty string, the widget will be made high enough to allow all contained widgets to have their natural height.

[-opaqueresize]{.cmdline} [opaqueResize]{.dbname} [OpaqueResize]{.dbclass}
: Specifies whether panes should be resized as a sash is moved (true), or if resizing should be deferred until the sash is placed (false). In the latter case, a "ghost" version of the sash is displayed during the resizing to show where the panes will be resized to when releasing the mouse button. This "ghost" version of the sash is the proxy. It's rendering can be configured using the **-proxybackground**, **-proxyborderwidth** and **-proxyrelief** options.

[-proxybackground]{.cmdline} [proxyBackground]{.dbname} [ProxyBackground]{.dbclass}
: Background color to use when drawing the proxy. If an empty string, the value of the **-background** option will be used.

[-proxyborderwidth]{.cmdline} [proxyBorderWidth]{.dbname} [ProxyBorderWidth]{.dbclass}
: Specifies the borderwidth of the proxy. May be any value accepted by [Tk\_GetPixels][GetPixels].

[-proxyrelief]{.cmdline} [proxyRelief]{.dbname} [ProxyRelief]{.dbclass}
: Relief to use when drawing the proxy. May be any of the standard Tk relief values. If an empty string, the value of the **-sashrelief** option will be used.

[-sashcursor]{.cmdline} [sashCursor]{.dbname} [SashCursor]{.dbclass}
: Mouse cursor to use when over a sash.  If null, **sb\_h\_double\_arrow** will be used for horizontal panedwindows, and **sb\_v\_double\_arrow** will be used for vertical panedwindows.

[-sashpad]{.cmdline} [sashPad]{.dbname} [SashPad]{.dbclass}
: Specifies the amount of padding to leave of each side of a sash.  May be any value accepted by [Tk\_GetPixels][GetPixels].

[-sashrelief]{.cmdline} [sashRelief]{.dbname} [SashRelief]{.dbclass}
: Relief to use when drawing a sash.  May be any of the standard Tk relief values.

[-sashwidth]{.cmdline} [sashWidth]{.dbname} [SashWidth]{.dbclass}
: Specifies the width of each sash.  May be any value accepted by [Tk\_GetPixels][GetPixels].

[-showhandle]{.cmdline} [showHandle]{.dbname} [ShowHandle]{.dbclass}
: Specifies whether sash handles should be shown.  May be any valid Tcl boolean value.

[-width]{.cmdline} [width]{.dbname} [Width]{.dbclass}
: Specifies a desired width for the overall panedwindow widget. May be any value accepted by [Tk\_GetPixels][GetPixels]. If an empty string, the widget will be made wide enough to allow all contained widgets to have their natural width.


# Description

The **panedwindow** command creates a new window (given by the *pathName* argument) and makes it into a panedwindow widget. Additional options, described above, may be specified on the command line or in the option database to configure aspects of the panedwindow such as its default background color and relief.  The **panedwindow** command returns the path name of the new window.

A panedwindow widget contains any number of panes, arranged horizontally or vertically, according to the value of the **-orient** option.  Each pane contains one widget, and each pair of panes is separated by a moveable (via mouse movements) sash.  Moving a sash causes the widgets on either side of the sash to be resized.

If a pane is not a child of the panedwindow, it must be higher in the stacking order than the panedwindow, otherwise the panedwindow will obscure it.  The easiest way to ensure this is to create the panedwindow first: the most recently created window is highest in the stacking order. Or, you can use the [raise] and [lower] commands.

# Widget command

The **panedwindow** command creates a new Tcl command whose name is the same as the path name of the panedwindow's window.  This command may be used to invoke various operations on the widget.  It has the following general form:

```
pathName option ?arg ...?
```

*PathName* is the name of the command, which is the same as the panedwindow widget's path name.  *Option* and the *arg*s determine the exact behavior of the command.  The following commands are possible for panedwindow widgets:

[pathName]{.ins} [add]{.sub} [window]{.arg} [window]{.optdot} [option value]{.optdot}
: Add one or more windows to the panedwindow, each in a separate pane. The arguments consist of the names of one or more windows followed by pairs of arguments that specify how to manage the windows. *Option* may have any of the values accepted by the **configure** subcommand.

[pathName]{.ins} [cget]{.sub} [option]{.arg}
: Returns the current value of the configuration option given by *option*.  *Option* may have any of the values accepted by the **panedwindow** command.

[pathName]{.ins} [configure]{.sub} [option]{.optarg} [value option value]{.optdot}
: Query or modify the configuration options of the widget.  If no *option* is specified, returns a list describing all of the available options for *pathName* (see [Tk\_ConfigureInfo][ConfigWidg] for information on the format of this list).  If *option* is specified with no *value*, then the command returns a list describing the one named option (this list will be identical to the corresponding sublist of the value returned if no *option* is specified).  If one or more *option-value* pairs are specified, then the command modifies the given widget option(s) to have the given value(s);  in this case the command returns an empty string. *Option* may have any of the values accepted by the **panedwindow** command.

[pathName]{.ins} [forget]{.sub} [window]{.arg} [window]{.optdot}
: Remove the pane containing *window* from the panedwindow.  All geometry management options for *window* will be forgotten.

[pathName]{.ins} [identify]{.sub} [x]{.arg} [y]{.arg}
: Identify the panedwindow component underneath the point given by *x* and *y*, in window coordinates.  If the point is over a sash or a sash handle, the result is a two element list containing the index of the sash or handle, and a word indicating whether it is over a sash or a handle, such as {0 sash} or {2 handle}.  If the point is over any other part of the panedwindow, the result is an empty list.

[pathName]{.ins} [panecget]{.sub} [window]{.arg} [option]{.arg}
: Query a management option for *window*.  *Option* may be any value allowed by the **paneconfigure** subcommand.

[pathName]{.ins} [paneconfigure]{.sub} [window]{.arg} [option]{.optarg} [value option value]{.optdot}
: Query or modify the management options for *window*.  If no *option* is specified, returns a list describing all of the available options for *pathName* (see [Tk\_ConfigureInfo][ConfigWidg] for information on the format of this list).  If *option* is specified with no *value*, then the command returns a list describing the one named option (this list will be identical to the corresponding sublist of the value returned if no *option* is specified).  If one or more *option-value* pairs are specified, then the command modifies the given widget option(s) to have the given value(s);  in this case the command returns an empty string.  The following options are supported:

    [-after]{.lit} [window]{.arg}
    : Insert the window after the window specified.  *window* should be the name of a window already managed by *pathName*.

    [-before]{.lit} [window]{.arg}
    : Insert the window before the window specified.  *window* should be the name of a window already managed by *pathName*.

    [-height]{.lit} [size]{.arg}
    : Specify a height for the window.  The height will be the outer dimension of the window including its border, if any.  If *size* is an empty string, or if **-height** is not specified, then the height requested internally by the window will be used initially; the height may later be adjusted by the movement of sashes in the panedwindow.  *Size* may be any value accepted by [Tk\_GetPixels][GetPixels].

    [-hide]{.lit} [boolean]{.arg}
    : Controls the visibility of a pane.  When the *boolean* is true (according to [Tcl\_GetBoolean][GetInt]) the pane will not be visible, but it will still be maintained in the list of panes.

    [-minsize]{.lit} [n]{.arg}
    : Specifies that the size of the window cannot be made less than *n*.  This constraint only affects the size of the widget in the paned dimension — the x dimension for horizontal panedwindows, the y dimension for vertical panedwindows.  May be any value accepted by [Tk\_GetPixels][GetPixels].

    [-padx]{.lit} [n]{.arg}
    : Specifies a non-negative value indicating how much extra space to leave on each side of the window in the X-direction.  The value may have any of the forms accepted by [Tk\_GetPixels][GetPixels].

    [-pady]{.lit} [n]{.arg}
    : Specifies a non-negative value indicating how much extra space to leave on each side of the window in the Y-direction.  The value may have any of the forms accepted by [Tk\_GetPixels][GetPixels].

    [-sticky]{.lit} [style]{.arg}
    : If a window's pane is larger than the requested dimensions of the window, this option may be used to position (or stretch) the window within its pane.  *Style*  is a string that contains zero or more of the characters **n**, **s**, **e** or **w**.  The string can optionally contains spaces or commas, but they are ignored.  Each letter refers to a side (north, south, east, or west) that the window will "stick" to.  If both **n** and **s** (or **e** and **w**) are specified, the window will be stretched to fill the entire height (or width) of its cavity.

    [-stretch]{.lit} [when]{.arg}
    : Controls how extra space is allocated to each of the panes. *When* is one of **always**, **first**, **last**, **middle**, and **never**. The panedwindow will calculate the required size of all its panes. Any remaining (or deficit) space will be distributed to those panes marked for stretching. The space will be distributed based on each panes current ratio of the whole.  The *when* values have the following definition:

    **always**
    : This pane will always stretch.

    **first**
    : Only if this pane is the first pane (left-most or top-most) will it stretch.

    **last**
    : Only if this pane is the last pane (right-most or bottom-most) will it stretch.  This is the default value.

    **middle**
    : Only if this pane is not the first or last pane will it stretch.

    **never**
    : This pane will never stretch.


[-width]{.lit} [size]{.arg}
: Specify a width for the window.  The width will be the outer dimension of the window including its border, if any.  If *size* is an empty string, or if **-width** is not specified, then the width requested internally by the window will be used initially; the width may later be adjusted by the movement of sashes in the panedwindow.  *Size* may be any value accepted by [Tk\_GetPixels][GetPixels].

[pathName]{.ins} [panes]{.sub}
: Returns an ordered list of the widgets managed by *pathName*.

[pathName]{.ins} [proxy]{.sub} [args]{.optarg}
: This command is used to query and change the position of the sash proxy, used for rubberband-style pane resizing. It can take any of the following forms:

    [pathName]{.ins} [proxy]{.sub} [coord]{.lit}
    : Return a list containing the x and y coordinates of the most recent proxy location.

    [pathName]{.ins} [proxy]{.sub} [forget]{.lit}
    : Remove the proxy from the display.

    [pathName]{.ins} [proxy]{.sub} [place]{.lit} [x]{.arg} [y]{.arg}
    : Place the proxy at the given *x* and *y* coordinates.


[pathName]{.ins} [sash]{.sub} [args]{.optarg}
: This command is used to query and change the position of sashes in the panedwindow.  It can take any of the following forms:

    [pathName]{.ins} [sash]{.sub} [coord]{.lit} [index]{.arg}
    : Return the current x and y coordinate pair for the sash given by *index*.  *Index* must be an integer between 0 and 1 less than the number of panes in the panedwindow.  The coordinates given are those of the top left corner of the region containing the sash.

    [pathName]{.ins} [sash]{.sub} [dragto]{.lit} [index]{.arg} [x]{.arg} [y]{.arg}
    : This command computes the difference between the given coordinates and the coordinates given to the last **sash mark** command for the given sash.  It then moves that sash the computed difference.  The return value is the empty string.

    [pathName]{.ins} [sash]{.sub} [mark]{.lit} [index]{.arg} [x]{.arg} [y]{.arg}
    : Records *x* and *y* for the sash given by *index*; used in conjunction with later **sash dragto** commands to move the sash.

    [pathName]{.ins} [sash]{.sub} [place]{.lit} [index]{.arg} [x]{.arg} [y]{.arg}
    : Place the sash given by *index* at the given coordinates.



# Resizing panes

A pane is resized by grabbing the sash (or sash handle if present) and dragging with the mouse.  This is accomplished via mouse motion bindings on the widget.  When a sash is moved, the sizes of the panes on each side of the sash, and thus the widgets in those panes, are adjusted.

When a pane is resized from outside (e.g. it is packed to expand and fill, and the containing toplevel is resized), space is added to the final (rightmost or bottommost) pane in the window.

Unlike child windows managed by e.g. pack or grid, the panes managed by a panedwindow do not change width or height to accommodate changes in the requested widths or heights of the panes, once these have become mapped. Therefore it may be advisable, particularly when creating layouts interactively, to not add a pane to the panedwindow widget until after the geometry requests of that pane has been finalized (i.e., all components of the pane inserted, all options affecting geometry set to their proper values, etc.).


[ConfigWidg]: ../TkCAPI/ConfigWidg.md
[GetInt]: ../TclCAPI/GetInt.md
[GetPixels]: ../TkCAPI/GetPixels.md
[lower]: lower.md
[options]: options.md
[raise]: raise.md

