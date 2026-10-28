---
CommandName: frame
title: frame
ManualSection: n
Version: 8.4
TclPart: Tk
TclDescription: Tk Built-In Commands
Links:
 - labelframe(n)
 - toplevel(n)
 - ttk::frame(n)
Keywords:
 - frame
 - widget
Copyright:
 - Copyright (c) 1990-1994 The Regents of the University of California.
 - Copyright (c) 1994-1996 Sun Microsystems, Inc.
---

# Name

frame - Create and manipulate 'frame' simple container widgets

# Synopsis

::: {.synopsis} :::
[frame]{.cmd} [pathName]{.arg} [options]{.optarg}
:::

# Standard options

::: {.standardoptions} :::
-borderwidth
-cursor
-highlightbackground
-highlightcolor
-highlightthickness
-padx
-pady
-relief
-takefocus
:::

See the [options] manual entry for details on the standard options.

# Widget-specific options

[-background]{.cmdline} [background]{.dbname} [Background]{.dbclass}
: This option is the same as the standard **-background** option except that its value may also be specified as an empty string. In this case, the widget will display no background or border, and no colors will be consumed from its colormap for its background and border. [An empty background will disable drawing the background image.]{version="9.0, TIP262"}

[[-backgroundimage]{.cmdline} [backgroundImage]{.dbname} [BackgroundImage]{.dbclass}]{version="9.0, TIP262"}
: [This specifies an image to display on the frame's background within the border of the frame (i.e., the image will be clipped by the frame's highlight ring and border, if either are present); subwidgets of the frame will be drawn on top. The image must have been created with the [image create][image] command. If specified as the empty string, no image will be displayed.]{version="9.0,"}

[-class]{.cmdline} [class]{.dbname} [Class]{.dbclass}
: Specifies a class for the window. This class will be used when querying the option database for the window's other options, and it will also be used later for other purposes such as bindings. The **-class** option may not be changed with the **configure** widget command.

[-colormap]{.cmdline} [colormap]{.dbname} [Colormap]{.dbclass}
: Specifies a colormap to use for the window. The value may be either **new**, in which case a new colormap is created for the window and its children, or the name of another window (which must be on the same screen and have the same visual as *pathName*), in which case the new window will use the colormap from the specified window. If the **-colormap** option is not specified, the new window uses the same colormap as its parent. This option may not be changed with the **configure** widget command.

[-container]{.cmdline} [container]{.dbname} [Container]{.dbclass}
: The value must be a boolean.  If true, it means that this window will be used as a container in which some other application will be embedded (for example, a Tk toplevel can be embedded using the **-use** option). The window will support the appropriate window manager protocols for things like geometry requests.  The window should not have any children of its own in this application. This option may not be changed with the **configure** widget command. Note that **-borderwidth**, **-padx** and **-pady** are ignored when configured as a container since a container has no border.

[-height]{.cmdline} [height]{.dbname} [Height]{.dbclass}
: Specifies the desired height for the window in any of the forms acceptable to [Tk\_GetPixels][GetPixels].  If this option is negative or zero then the window will not request any size at all.  Note that this sets the total height of the frame, any **-borderwidth** or similar is not added.  Normally **-height** should not be used if a propagating geometry manager, such as [grid] or [pack], is used within the frame since the geometry manager will override the height of the frame.

[-tile]{.cmdline} [tile]{.dbname} [Tile]{.dbclass}
: [This specifies how to draw the background image (see **-backgroundimage**) on the frame. If true (according to [Tcl\_GetBoolean][GetInt]), the image will be tiled to fill the whole frame, with the origin of the first copy of the image being the top left of the interior of the frame. If false (the default), the image will be centered within the frame.]{version="9.0, TIP262"}

[-visual]{.cmdline} [visual]{.dbname} [Visual]{.dbclass}
: Specifies visual information for the new window in any of the forms accepted by [Tk\_GetVisual][GetVisual]. If this option is not specified, the new window will use the same visual as its parent. The **-visual** option may not be modified with the **configure** widget command.

[-width]{.cmdline} [width]{.dbname} [Width]{.dbclass}
: Specifies the desired width for the window in any of the forms acceptable to [Tk\_GetPixels][GetPixels].  If this option is negative or zero then the window will not request any size at all.  Note that this sets the total width of the frame, any **-borderwidth** or similar is not added.  Normally **-width** should not be used if a propagating geometry manager, such as [grid] or [pack], is used within the frame since the geometry manager will override the width of the frame.


# Description

The **frame** command creates a new window (given by the *pathName* argument) and makes it into a frame widget. Additional options, described above, may be specified on the command line or in the option database to configure aspects of the frame such as its background color and relief.  The **frame** command returns the path name of the new window.

A frame is a simple widget.  Its primary purpose is to act as a spacer or container for complex window layouts.  The only features of a frame are its background and an optional 3-D border to make the frame appear raised or sunken.

# Widget command

The **frame** command creates a new Tcl command whose name is the same as the path name of the frame's window.  This command may be used to invoke various operations on the widget.  It has the following general form:

```
pathName option ?arg ...?
```

*PathName* is the name of the command, which is the same as the frame widget's path name.  *Option* and the *arg*s determine the exact behavior of the command.  The following commands are possible for frame widgets:

[pathName]{.ins} [cget]{.sub} [option]{.arg}
: Returns the current value of the configuration option given by *option*. *Option* may have any of the values accepted by the **frame** command.

[pathName]{.ins} [configure]{.sub} [option]{.optarg} [value option value]{.optdot}
: Query or modify the configuration options of the widget. If no *option* is specified, returns a list describing all of the available options for *pathName* (see [Tk\_ConfigureInfo][ConfigWidg] for information on the format of this list).  If *option* is specified with no *value*, then the command returns a list describing the one named option (this list will be identical to the corresponding sublist of the value returned if no *option* is specified).  If one or more *option-value* pairs are specified, then the command modifies the given widget option(s) to have the given value(s);  in this case the command returns an empty string. *Option* may have any of the values accepted by the **frame** command.


# Bindings

When a new frame is created, it has no default event bindings: frames are not intended to be interactive.


[ConfigWidg]: ../TkCAPI/ConfigWidg.md
[GetInt]: ../TclCAPI/GetInt.md
[GetPixels]: ../TkCAPI/GetPixels.md
[GetVisual]: ../TkCAPI/GetVisual.md
[grid]: grid.md
[image]: image.md
[options]: options.md
[pack]: pack.md

