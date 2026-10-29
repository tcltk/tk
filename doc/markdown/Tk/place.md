---
CommandName: place
title: place
ManualSection: n
Version: unknown
TclPart: Tk
TclDescription: Tk Built-In Commands
Links:
 - grid(n)
 - pack(n)
Keywords:
 - geometry manager
 - height
 - location
 - container
 - place
 - rubber sheet
 - content
 - width
Copyright:
 - Copyright (c) 1992 The Regents of the University of California.
 - Copyright (c) 1994-1996 Sun Microsystems, Inc.
---

# Name

place - Geometry manager for fixed or rubber-sheet placement

# Synopsis

::: {.synopsis} :::
[place]{.cmd} [option]{.arg} [arg]{.arg} [arg]{.optdot}
:::

# Description

The placer is a geometry manager for Tk. It provides simple fixed placement of windows, where you specify the exact size and location of one window, called the *content*, within another window, called the *container*. The placer also provides rubber-sheet placement, where you specify the size and location of the content in terms of the dimensions of the container, so that the content changes size and location in response to changes in the size of the container. Lastly, the placer allows you to mix these styles of placement so that, for example, the content has a fixed width and height but is centered inside the container.

**place** *window option value* ?*option value ...*?
: Arrange for the placer to manage the geometry of a content whose pathName is *window*.  The remaining arguments consist of one or more *option-value* pairs that specify the way in which *window*'s geometry is managed.  *Option* may have any of the values accepted by the **place configure** command.

[place]{.cmd} [configure]{.sub} [window]{.arg} [option]{.optarg} [value option value]{.optdot}
: Query or modify the geometry options of the content given by *window*.  If no *option* is specified, this command returns a list describing the available options (see [Tk\_ConfigureInfo][ConfigWidg] for information on the format of this list).  If *option* is specified with no *value*, then the command returns a list describing the one named option (this list will be identical to the corresponding sublist of the value returned if no *option* is specified).  If one or more *option-value* pairs are specified, then the command modifies the given option(s) to have the given value(s); in this case the command returns an empty string.

    The following *option-value* pairs are supported:

    [-anchor]{.lit} [where]{.arg}
    : *Where* specifies which point of *window* is to be positioned at the (x,y) location selected by the **-x**, **-y**, **-relx**, and **-rely** options. The anchor point is in terms of the outer area of *window* including its border, if any. Thus if *where* is **se** then the lower-right corner of *window*'s border will appear at the given (x,y) location in the container. The anchor position defaults to **nw**.

    **-bordermode** *mode*
    : *Mode* determines the degree to which borders within the container are used in determining the placement of the content. The default and most common value is **inside**. In this case the placer considers the area of the container to be the innermost area of the container, inside any border: an option of **-x 0** corresponds to an x-coordinate just inside the border and an option of **-relwidth 1.0** means *window* will fill the area inside the container's border.


    If *mode* is **outside** then the placer considers the area of the container to include its border; this mode is typically used when placing *window* outside its container, as with the options **-x 0 -y 0 -anchor ne**. Lastly, *mode* may be specified as **ignore**, in which case borders are ignored:  the area of the container is considered to be its official X area, which includes any internal border but no external border.  A bordermode of **ignore** is probably not very useful.

**-height** *size*
: *Size* specifies the height for *window* in screen units (i.e. any of the forms accepted by [Tk\_GetPixels][GetPixels]). The height will be the outer dimension of *window* including its border, if any. If *size* is an empty string, or if no **-height** or **-relheight** option is specified, then the height requested internally by the window will be used.

[-in]{.lit} [container]{.arg}
: *Container* specifies the path name of the window relative to which *window* is to be placed. *Container* must either be *window*'s parent or a descendant of *window*'s parent. In addition, *container* and *window* must both be descendants of the same top-level window. These restrictions are necessary to guarantee that *window* is visible whenever *container* is visible. If this option is not specified then the other window defaults to *window*'s parent.

[-relheight]{.lit} [size]{.arg}
: *Size* specifies the height for *window*. In this case the height is specified as a floating-point number relative to the height of the container: 0.5 means *window* will be half as high as the container, 1.0 means *window* will have the same height as the container, and so on. If both **-height** and **-relheight** are specified for a content, their values are summed.  For example, **-relheight 1.0 -height -2** makes the content 2 pixels shorter than the container.

[-relwidth]{.lit} [size]{.arg}
: *Size* specifies the width for *window*. In this case the width is specified as a floating-point number relative to the width of the container: 0.5 means *window* will be half as wide as the container, 1.0 means *window* will have the same width as the container, and so on. If both **-width** and **-relwidth** are specified for a content, their values are summed.  For example, **-relwidth 1.0 -width 5** makes the content 5 pixels wider than the container.

[-relx]{.lit} [location]{.arg}
: *Location* specifies the x-coordinate within the container window of the anchor point for *window*. In this case the location is specified in a relative fashion as a floating-point number:  0.0 corresponds to the left edge of the container and 1.0 corresponds to the right edge of the container. *Location* need not be in the range 0.0-1.0. If both **-x** and **-relx** are specified for a content then their values are summed.  For example, **-relx 0.5 -x -2** positions the left edge of the content 2 pixels to the left of the center of its container.

[-rely]{.lit} [location]{.arg}
: *Location* specifies the y-coordinate within the container window of the anchor point for *window*. In this case the value is specified in a relative fashion as a floating-point number:  0.0 corresponds to the top edge of the container and 1.0 corresponds to the bottom edge of the container. *Location* need not be in the range 0.0-1.0. If both **-y** and **-rely** are specified for a content then their values are summed.  For example, **-rely 0.5 -x 3** positions the top edge of the content 3 pixels below the center of its container.

[-width]{.lit} [size]{.arg}
: *Size* specifies the width for *window* in screen units (i.e. any of the forms accepted by [Tk\_GetPixels][GetPixels]). The width will be the outer width of *window* including its border, if any. If *size* is an empty string, or if no **-width** or **-relwidth** option is specified, then the width requested internally by the window will be used.

[-x]{.lit} [location]{.arg}
: *Location* specifies the x-coordinate within the container window of the anchor point for *window*. The location is specified in screen units (i.e. any of the forms accepted by [Tk\_GetPixels][GetPixels]) and need not lie within the bounds of the container window.

[-y]{.lit} [location]{.arg}
: *Location* specifies the y-coordinate within the container window of the anchor point for *window*. The location is specified in screen units (i.e. any of the forms accepted by [Tk\_GetPixels][GetPixels]) and need not lie within the bounds of the container window.


If the same value is specified separately with two different options, such as **-x** and **-relx**, then the most recent option is used and the older one is ignored.

[place]{.cmd} [content]{.sub} [window]{.arg}
: Returns a list of all the content windows for which *window* is the container. If there is no content for *window* then an empty string is returned.

[place]{.cmd} [forget]{.sub} [window]{.arg}
: Causes the placer to stop managing the geometry of *window*.  As a side effect of this command *window* will be unmapped so that it does not appear on the screen.  If *window* is not currently managed by the placer then the command has no effect.  This command returns an empty string.

[place]{.cmd} [info]{.sub} [window]{.arg}
: Returns a list giving the current configuration of *window*. The list consists of *option-value* pairs in exactly the same form as might be specified to the **place configure** command.

[place]{.cmd} [slaves]{.sub} [window]{.arg}
: Synonym for **place content** *window*.


If the configuration of a window has been retrieved with **place info**, that configuration can be restored later by first using **place forget** to erase any existing information for the window and then invoking **place configure** with the saved information.

# Fine points

It is not necessary for the container window to be the parent of the content window. This feature is useful in at least two situations. First, for complex window layouts it means you can create a hierarchy of subwindows whose only purpose is to assist in the layout of the parent. The "real children" of the parent (i.e. the windows that are significant for the application's user interface) can be children of the parent yet be placed inside the windows of the geometry-management hierarchy. This means that the path names of the "real children" do not reflect the geometry-management hierarchy and users can specify options for the real children without being aware of the structure of the geometry-management hierarchy.

A second reason for having a container different than the content's parent is to tie two siblings together. For example, the placer can be used to force a window always to be positioned centered just below one of its siblings by specifying the configuration

```
-in sibling -relx 0.5 -rely 1.0 -anchor n -bordermode outside
```

Whenever the sibling is repositioned in the future, the content will be repositioned as well.

Unlike many other geometry managers (such as the packer) the placer does not make any attempt to manipulate the geometry of the container windows or the parents of content windows (i.e. it does not set their requested sizes). To control the sizes of these windows, make them windows like frames and canvases that provide configuration options for this purpose.

# Example

Make the label occupy the middle bit of the toplevel, no matter how it is resized:

```
label .l -text "In the\nMiddle!" -bg black -fg white
place .l -relwidth .3 -relx .35 -relheight .3 -rely .35
```


[ConfigWidg]: ../TkCAPI/ConfigWidg.md
[GetPixels]: ../TkCAPI/GetPixels.md

