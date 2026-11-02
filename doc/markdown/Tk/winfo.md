---
CommandName: winfo
title: winfo
ManualSection: n
Version: 4.3
TclPart: Tk
TclDescription: Tk Built-In Commands
Keywords:
 - atom
 - children
 - class
 - geometry
 - height
 - identifier
 - information
 - interpreters
 - mapped
 - parent
 - path name
 - screen
 - virtual root
 - width
 - window
Copyright:
 - Copyright (c) 1990-1994 The Regents of the University of California.
 - Copyright (c) 1994-1997 Sun Microsystems, Inc.
---

# Name

winfo - Return window-related information

# Synopsis

::: {.synopsis} :::
[winfo]{.cmd} [option]{.arg} [arg]{.optdot}
:::

# Description

The **winfo** command is used to retrieve information about windows managed by Tk.  It can take any of a number of different forms, depending on the *option* argument.  The legal forms are:

[winfo]{.cmd} [atom]{.sub} [[-displayof]{.lit} [window]{.arg}]{.optarg} [name]{.arg}
: Returns a decimal string giving the integer identifier for the atom whose name is *name*.  If no atom exists with the name *name* then a new one is created. If the **-displayof** option is given then the atom is looked up on the display of *window*;  otherwise it is looked up on the display of the application's main window.

[winfo]{.cmd} [atomname]{.sub} [[-displayof]{.lit} [window]{.arg}]{.optarg} [id]{.arg}
: Returns the textual name for the atom whose integer identifier is *id*. If the **-displayof** option is given then the identifier is looked up on the display of *window*;  otherwise it is looked up on the display of the application's main window. This command is the inverse of the **winfo atom** command. It generates an error if no such atom exists.

[winfo]{.cmd} [cells]{.sub} [window]{.arg}
: Returns a decimal string giving the number of cells in the color map for *window*.

[winfo]{.cmd} [children]{.sub} [window]{.arg}
: Returns a list containing the path names of all the children of *window*. Top-level windows are returned as children of their logical parents. The list is in stacking order, with the lowest window first, except for Top-level windows which are not returned in stacking order. Use the [wm stackorder][wm] command to query the stacking order of Top-level windows.

[winfo]{.cmd} [class]{.sub} [window]{.arg}
: Returns the class name for *window*.

[winfo]{.cmd} [colormapfull]{.sub} [window]{.arg}
: Returns 1 if the colormap for *window* is known to be full, 0 otherwise.  The colormap for a window is "known" to be full if the last attempt to allocate a new color on that window failed and this application has not freed any colors in the colormap since the failed allocation.

[winfo]{.cmd} [containing]{.sub} [[-displayof]{.lit} [window]{.arg}]{.optarg} [rootX]{.arg} [rootY]{.arg}
: Returns the path name for the window containing the point given by *rootX* and *rootY*. *RootX* and *rootY* are specified in screen units (i.e. any form acceptable to [Tk\_GetPixels][GetPixels]) in the coordinate system of the root window (if a virtual-root window manager is in use then the coordinate system of the virtual root window is used). If the **-displayof** option is given then the coordinates refer to the screen containing *window*;  otherwise they refer to the screen of the application's main window. If no window in this application contains the point then an empty string is returned. An empty string is also returned if the point lies in the title bar or border of its highest containing toplevel in this application. (Note that with some window managers the borders may be invisible.) In selecting the containing window, children are given higher priority than parents and among siblings the highest one in the stacking order is chosen.

[winfo]{.cmd} [depth]{.sub} [window]{.arg}
: Returns a decimal string giving the depth of *window* (number of bits per pixel).

[winfo]{.cmd} [exists]{.sub} [window]{.arg}
: Returns 1 if there exists a window named *window*, 0 if no such window exists.

[winfo]{.cmd} [fpixels]{.sub} [window]{.arg} [number]{.arg}
: Returns a floating-point value giving the number of pixels in *window* corresponding to the distance given by *number*. *Number* may be specified in any of the forms acceptable to [Tk\_GetScreenMM][GetPixels], such as "2.0c" or "1i". The return value may be fractional;  for an integer value, use **winfo pixels**.

[winfo]{.cmd} [geometry]{.sub} [window]{.arg}
: Returns the geometry for *window*, in the form *width***x***height***+***x***+***y*.  All dimensions are in pixels.

[winfo]{.cmd} [height]{.sub} [window]{.arg}
: Returns a decimal string giving *window*'s height in pixels. When a window is first created its height will be 1 pixel;  the height will eventually be changed by a geometry manager to fulfil the window's needs. If you need the true height immediately after creating a widget, invoke [update] to force the geometry manager to arrange it, or use **winfo reqheight** to get the window's requested height instead of its actual height.

[winfo]{.cmd} [id]{.sub} [window]{.arg}
: Returns a hexadecimal string giving a low-level platform-specific identifier for *window*.  On Unix platforms, this is the X window identifier.  Under Windows, this is the Windows HWND.  On the Macintosh the value has no meaning outside Tk.

[winfo]{.cmd} [interps]{.sub} [[-displayof]{.lit} [window]{.arg}]{.optarg}
: Returns a list whose members are the names of all Tcl interpreters (e.g. all Tk-based applications) currently registered for a particular display. If the **-displayof** option is given then the return value refers to the display of *window*;  otherwise it refers to the display of the application's main window.

[winfo]{.cmd} [isdark]{.sub} [window]{.arg}
: On Aqua and Windows returns **1** if *window* is in "dark mode", and **0** otherwise.   Returns **0** on linux.

[winfo]{.cmd} [ismapped]{.sub} [window]{.arg}
: Returns **1** if *window* is currently mapped, **0** otherwise.

[winfo]{.cmd} [manager]{.sub} [window]{.arg}
: Returns the name of the geometry manager currently responsible for *window*, or an empty string if *window* is not managed by any geometry manager. The name is usually the name of the Tcl command for the geometry manager, such as [pack] or [place]. If the geometry manager is a widget, such as canvases or text, the name is the widget's class command, such as [canvas].

[winfo]{.cmd} [name]{.sub} [window]{.arg}
: Returns *window*'s name (i.e. its name within its parent, as opposed to its full path name). The command **winfo name .** will return the name of the application.

[winfo]{.cmd} [parent]{.sub} [window]{.arg}
: Returns the path name of *window*'s parent, or an empty string if *window* is the main window of the application.

[winfo]{.cmd} [pathname]{.sub} [[-displayof]{.lit} [window]{.arg}]{.optarg} [id]{.arg}
: Returns the path name of the window whose X identifier is *id*. *Id* must be a decimal, hexadecimal, or octal integer and must correspond to a window in the invoking application. If the **-displayof** option is given then the identifier is looked up on the display of *window*;  otherwise it is looked up on the display of the application's main window.

[winfo]{.cmd} [pixels]{.sub} [window]{.arg} [number]{.arg}
: Returns the number of pixels in *window* corresponding to the distance given by *number*. *Number* may be specified in any of the forms acceptable to [Tk\_GetPixels][GetPixels], such as "2.0c" or "1i". The result is rounded to the nearest integer value;  for a fractional result, use **winfo fpixels**.

[winfo]{.cmd} [pointerx]{.sub} [window]{.arg}
: If the mouse pointer is on the same screen as *window*, returns the pointer's x coordinate, measured in pixels in the screen's root window. If a virtual root window is in use on the screen, the position is measured in the virtual root. If the mouse pointer is not on the same screen as *window* then -1 is returned.

[winfo]{.cmd} [pointerxy]{.sub} [window]{.arg}
: If the mouse pointer is on the same screen as *window*, returns a list with two elements, which are the pointer's x and y coordinates measured in pixels in the screen's root window. If a virtual root window is in use on the screen, the position is computed in the virtual root. If the mouse pointer is not on the same screen as *window* then both of the returned coordinates are -1.

[winfo]{.cmd} [pointery]{.sub} [window]{.arg}
: If the mouse pointer is on the same screen as *window*, returns the pointer's y coordinate, measured in pixels in the screen's root window. If a virtual root window is in use on the screen, the position is computed in the virtual root. If the mouse pointer is not on the same screen as *window* then -1 is returned.

[winfo]{.cmd} [reqheight]{.sub} [window]{.arg}
: Returns a decimal string giving *window*'s requested height, in pixels.  This is the value used by *window*'s geometry manager to compute its geometry.

[winfo]{.cmd} [reqwidth]{.sub} [window]{.arg}
: Returns a decimal string giving *window*'s requested width, in pixels.  This is the value used by *window*'s geometry manager to compute its geometry.

[winfo]{.cmd} [rgb]{.sub} [window]{.arg} [color]{.arg}
: Returns a list containing three decimal values in the range 0 to 65535, which are the red, green, and blue intensities that correspond to *color* in the window given by *window*.  *Color* may be specified in any of the forms acceptable for a color option.

[winfo]{.cmd} [rootx]{.sub} [window]{.arg}
: Returns a decimal string giving the x-coordinate, in the root window of the screen, of the upper-left corner of *window*'s border (or *window* if it has no border).

[winfo]{.cmd} [rooty]{.sub} [window]{.arg}
: Returns a decimal string giving the y-coordinate, in the root window of the screen, of the upper-left corner of *window*'s border (or *window* if it has no border).

[winfo]{.cmd} [screen]{.sub} [window]{.arg}
: Returns the name of the screen associated with *window*, in the form *displayName*.*screenIndex*.

[winfo]{.cmd} [screencells]{.sub} [window]{.arg}
: Returns a decimal string giving the number of cells in the default color map for *window*'s screen.

[winfo]{.cmd} [screendepth]{.sub} [window]{.arg}
: Returns a decimal string giving the depth of the root window of *window*'s screen (number of bits per pixel).

[winfo]{.cmd} [screenheight]{.sub} [window]{.arg}
: Returns a decimal string giving the height of *window*'s screen, in pixels.

[winfo]{.cmd} [screenmmheight]{.sub} [window]{.arg}
: Returns a decimal string giving the height of *window*'s screen, in millimeters.

[winfo]{.cmd} [screenmmwidth]{.sub} [window]{.arg}
: Returns a decimal string giving the width of *window*'s screen, in millimeters.

[winfo]{.cmd} [screenvisual]{.sub} [window]{.arg}
: Returns one of the following strings to indicate the default visual class for *window*'s screen: **directcolor**, **grayscale**, **pseudocolor**, **staticcolor**, **staticgray**, or **truecolor**.

[winfo]{.cmd} [screenwidth]{.sub} [window]{.arg}
: Returns a decimal string giving the width of *window*'s screen, in pixels.

[winfo]{.cmd} [server]{.sub} [window]{.arg}
: Returns a string containing information about the server for *window*'s display.  The exact format of this string may vary from platform to platform.  For X servers the string has the form "**X***major***R***minor vendor vendorVersion*" where *major* and *minor* are the version and revision numbers provided by the server (e.g., **X11R5**), *vendor* is the name of the vendor for the server, and *vendorRelease* is an integer release number provided by the server.

[winfo]{.cmd} [toplevel]{.sub} [window]{.arg}
: Returns the path name of the top-of-hierarchy window containing *window*. In standard Tk this will always be a [toplevel] widget, but extensions may create other kinds of top-of-hierarchy widgets.

[winfo]{.cmd} [viewable]{.sub} [window]{.arg}
: Returns 1 if *window* and all of its ancestors up through the nearest toplevel window are mapped.  Returns 0 if any of these windows are not mapped.

[winfo]{.cmd} [visual]{.sub} [window]{.arg}
: Returns one of the following strings to indicate the visual class for *window*: **directcolor**, **grayscale**, **pseudocolor**, **staticcolor**, **staticgray**, or **truecolor**.

[winfo]{.cmd} [visualid]{.sub} [window]{.arg}
: Returns the X identifier for the visual for *window*.

[winfo]{.cmd} [visualsavailable]{.sub} [window]{.arg} [includeids]{.optlit}
: Returns a list whose elements describe the visuals available for *window*'s screen. Each element consists of a visual class followed by an integer depth. The class has the same form as returned by **winfo visual**. The depth gives the number of bits per pixel in the visual. In addition, if the **includeids** argument is provided, then the depth is followed by the X identifier for the visual.

[winfo]{.cmd} [vrootheight]{.sub} [window]{.arg}
: Returns the height of the virtual root window associated with *window* if there is one;  otherwise returns the height of *window*'s screen.

[winfo]{.cmd} [vrootwidth]{.sub} [window]{.arg}
: Returns the width of the virtual root window associated with *window* if there is one;  otherwise returns the width of *window*'s screen.

[winfo]{.cmd} [vrootx]{.sub} [window]{.arg}
: Returns the x-offset of the virtual root window associated with *window*, relative to the root window of its screen. This is normally either zero or negative. Returns 0 if there is no virtual root window for *window*.

[winfo]{.cmd} [vrooty]{.sub} [window]{.arg}
: Returns the y-offset of the virtual root window associated with *window*, relative to the root window of its screen. This is normally either zero or negative. Returns 0 if there is no virtual root window for *window*.

[winfo]{.cmd} [width]{.sub} [window]{.arg}
: Returns a decimal string giving *window*'s width in pixels. When a window is first created its width will be 1 pixel;  the width will eventually be changed by a geometry manager to fulfil the window's needs. If you need the true width immediately after creating a widget, invoke [update] to force the geometry manager to arrange it, or use **winfo reqwidth** to get the window's requested width instead of its actual width.

[winfo]{.cmd} [x]{.sub} [window]{.arg}
: Returns a decimal string giving the x-coordinate, in *window*'s parent, of the upper-left corner of *window*'s border (or *window* if it has no border).

[winfo]{.cmd} [y]{.sub} [window]{.arg}
: Returns a decimal string giving the y-coordinate, in *window*'s parent, of the upper-left corner of *window*'s border (or *window* if it has no border).


# Example

Print where the mouse pointer is and what window it is currently over:

```
lassign [winfo pointerxy .] x y
puts -nonewline "Mouse pointer at ($x,$y) which is "
set win [winfo containing $x $y]
if {$win eq ""} {
    puts "over no window"
} else {
    puts "over $win"
}
```


[canvas]: canvas.md
[GetPixels]: ../TkCAPI/GetPixels.md
[pack]: pack.md
[place]: place.md
[toplevel]: toplevel.md
[update]: ../Tcl/update.md
[wm]: wm.md

