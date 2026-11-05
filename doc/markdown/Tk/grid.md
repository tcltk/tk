---
CommandName: grid
title: grid
ManualSection: n
Version: 8.5
TclPart: Tk
TclDescription: Tk Built-In Commands
Links:
 - pack(n)
 - place(n)
Keywords:
 - geometry manager
 - location
 - grid
 - cell
 - propagation
 - size
 - pack
Copyright:
 - Copyright (c) 1996 Sun Microsystems, Inc.
---

# Name

grid - Geometry manager that arranges widgets in a grid

# Synopsis

::: {.synopsis} :::
[grid]{.cmd} [option]{.arg} [arg]{.arg} [arg]{.optdot}
:::

# Description

The **grid** command is used to communicate with the grid geometry manager that arranges widgets in rows and columns inside of another window, called the geometry container (or container window). The **grid** command can have any of several forms, depending on the *option* argument:

**grid** *window* ?*window ...*? ?*options*?
: If the first argument to **grid** is suitable as the first window argument to **grid configure**, either a window name (any value starting with **.**) or one of the characters **x** or **^** (see the [Relative placement] section below), then the command is processed in the same way as **grid configure**.

[grid]{.cmd} [anchor]{.sub} [window]{.arg} [anchor]{.optarg}
: The anchor value controls how to place the grid within the container window when no row/column has any weight.  See [The grid algorithm] below for further details.  The default *anchor* is *nw*.

[grid]{.cmd} [bbox]{.sub} [window]{.arg} [column row]{.optarg} [column2 row2]{.optarg}
: With no arguments, the bounding box (in pixels) of the grid is returned. The return value consists of 4 integers.  The first two are the pixel offset from the container window (x then y) of the top-left corner of the grid, and the second two integers are the width and height of the grid, also in pixels.  If a single *column* and *row* is specified on the command line, then the bounding box for that cell is returned, where the top left cell is numbered from zero.  If both *column* and *row* arguments are specified, then the bounding box spanning the rows and columns indicated is returned.

[grid]{.cmd} [columnconfigure]{.sub} [window]{.arg} [index]{.arg} [-option value...]{.optarg}
: Query or set the column properties of the *index* column of the geometry container, *window*. The valid options are **-minsize**, **-weight**, **-uniform** and **-pad**. If one or more options are provided, then *index* may be given as a list of column indices to which the configuration options will operate on. Indices may be integers, window names or the keyword *all*. For *all* the options apply to all columns currently occupied be content windows. For a window name, that window must be a content of this container and the options apply to all columns currently occupied be the content. The **-minsize** option sets the minimum size, in screen units, that will be permitted for this column. The **-weight** option (an integer value) sets the relative weight for apportioning any extra spaces among columns. A weight of zero (0) indicates the column will not deviate from its requested size.  A column whose weight is two will grow at twice the rate as a column of weight one when extra space is allocated to the layout. The **-uniform** option, when a non-empty value is supplied, places the column in a *uniform group* with other columns that have the same value for **-uniform**.  The space for columns belonging to a uniform group is allocated so that their sizes are always in strict proportion to their **-weight** values.  See [The grid algorithm] below for further details. The **-pad** option specifies the number of screen units that will be added to the largest window contained completely in that column when the grid geometry manager requests a size from the containing window. If only an option is specified, with no value, the current value of that option is returned. If only the container window and index is specified, all the current settings are returned in a list of "*-option value*" pairs.

[grid]{.cmd} [configure]{.sub} [window]{.arg} [window]{.optdot} [options]{.optarg}
: The arguments consist of the names of one or more content windows followed by pairs of arguments that specify how to manage the content. The characters **-**,  **x** and **^**, can be specified instead of a window name to alter the default location of a *window*, as described in the [Relative placement] section, below. The following options are supported:

    [-column]{.lit} [n]{.arg}
    : Insert the window so that it occupies the *n*th column in the grid. Column numbers start with 0.  If this option is not supplied, then the window is arranged just to the right of previous window specified on this call to **grid**, or column "0" if it is the first window.  For each **x** that immediately precedes the *window*, the column position is incremented by one.  Thus the **x** represents a blank column for this row in the grid.

    [-columnspan]{.lit} [n]{.arg}
    : Insert the window so that it occupies *n* columns in the grid. The default is one column, unless the window name is followed by a **-**, in which case the columnspan is incremented once for each immediately following **-**.

    [-in]{.lit} [container]{.arg}
    : Insert the window(s) in the container window given by *container*.  The default is the first window's parent window.

    [-ipadx]{.lit} [amount]{.arg}
    : The *amount* specifies how much horizontal internal padding to leave on each side of the content.  This is space is added inside the content border. The *amount* must be a valid screen distance, such as **2** or **.5c**. It defaults to 0.

    [-ipady]{.lit} [amount]{.arg}
    : The *amount* specifies how much vertical internal padding to leave on the top and bottom of the content. This space is added inside the content border. The *amount*  defaults to 0.

    [-padx]{.lit} [amount]{.arg}
    : The *amount* specifies how much horizontal external padding to leave on each side of the content, in screen units. *Amount* may be a list of two values to specify padding for left and right separately. The *amount* defaults to 0. This space is added outside the content border.

    [-pady]{.lit} [amount]{.arg}
    : The *amount* specifies how much vertical external padding to leave on the top and bottom of the content, in screen units. *Amount* may be a list of two values to specify padding for top and bottom separately. The *amount* defaults to 0. This space is added outside the content border.

    [-row]{.lit} [n]{.arg}
    : Insert the content so that it occupies the *n*th row in the grid. Row numbers start with 0.  If this option is not supplied, then the content is arranged on the same row as the previous content specified on this call to **grid**, or the next row after the highest occupied row if this is the first content.

    [-rowspan]{.lit} [n]{.arg}
    : Insert the content so that it occupies *n* rows in the grid. The default is one row.  If the next **grid** command contains **^** characters instead of *content* that line up with the columns of this *content*, then the **rowspan** of this *content* is extended by one.

    [-sticky]{.lit} [style]{.arg}
    : If a content's cell is larger than its requested dimensions, this option may be used to position (or stretch) the content within its cell. *Style*  is a string that contains zero or more of the characters **n**, **s**, **e** or **w**. The string can optionally contain spaces or commas, but they are ignored.  Each letter refers to a side (north, south, east, or west) that the content will "stick" to.  If both **n** and **s** (or **e** and **w**) are specified, the content will be stretched to fill the entire height (or width) of its cavity.  The **-sticky** option subsumes the combination of **-anchor** and **-fill** that is used by [pack]. The default is "", which causes the content to be centered in its cavity, at its requested size.


    If any of the content is already managed by the geometry manager then any unspecified options for them retain their previous values rather than receiving default values.

[grid]{.cmd} [content]{.sub} [window]{.arg} [-option value]{.optarg}
: If no options are supplied, a list of all of the content in *window* is returned, most recently managed first. *Option* can be either **-row** or **-column** which causes only the content in the row (or column) specified by *value* to be returned.

[grid]{.cmd} [forget]{.sub} [window]{.arg} [window]{.optdot}
: Removes each of the *window*s from grid for its container and unmaps their windows. The content will no longer be managed by the grid geometry manager. The configuration options for that window are forgotten, so that if the window is managed once more by the grid geometry manager, the initial default settings are used.

    [If the last content window of the container becomes unmanaged, this will also send the virtual event **<<NoManagedChild>>** to the container; the container may choose to resize itself (or otherwise respond) to such a change.]{version="TIP 518"}

[grid]{.cmd} [info]{.sub} [window]{.arg}
: Returns a list whose elements are the current configuration state of the content given by *window* in the same option-value form that might be specified to **grid configure**. The first two elements of the list are "**-in** *container*" where *container* is the windows's container window.

[grid]{.cmd} [location]{.sub} [window]{.arg} [x]{.arg} [y]{.arg}
: Given  *x* and *y* values in screen units relative to the container window, the column and row number at that *x* and *y* location is returned. For locations that are above or to the left of the grid, **-1** is returned.

[grid]{.cmd} [propagate]{.sub} [window]{.arg} [boolean]{.optarg}
: If *boolean* has a true boolean value such as **1** or **on** then propagation is enabled for *window*, which must be a window name (see [Geometry propagation] below). If *boolean* has a false boolean value then propagation is disabled for *window*. In either of these cases an empty string is returned. If *boolean* is omitted then the command returns **0** or **1** to indicate whether propagation is currently enabled for *window*.

    Propagation is enabled by default.

[grid]{.cmd} [rowconfigure]{.sub} [window]{.arg} [index]{.arg} [-option value...]{.optarg}
: Query or set the row properties of the *index* row of the geometry container, *window*. The valid options are **-minsize**, **-weight**, **-uniform** and **-pad**. If one or more options are provided, then *index* may be given as a list of row indices to which the configuration options will operate on. Indices may be integers, window names or the keyword *all*. For *all* the options apply to all rows currently occupied by content windows. For a window name, that window must be a content window of this container and the options apply to all rows currently occupied by the container window. The **-minsize** option sets the minimum size, in screen units, that will be permitted for this row. The **-weight** option (an integer value) sets the relative weight for apportioning any extra spaces among rows. A weight of zero (0) indicates the row will not deviate from its requested size.  A row whose weight is two will grow at twice the rate as a row of weight one when extra space is allocated to the layout. The **-uniform** option, when a non-empty value is supplied, places the row in a *uniform group* with other rows that have the same value for **-uniform**.  The space for rows belonging to a uniform group is allocated so that their sizes are always in strict proportion to their **-weight** values.  See [The grid algorithm] below for further details. The **-pad** option specifies the number of screen units that will be added to the largest window contained completely in that row when the grid geometry manager requests a size from the containing window. If only an option is specified, with no value, the current value of that option is returned. If only the container window and index is specified, all the current settings are returned in a list of "-option value" pairs.

[grid]{.cmd} [remove]{.sub} [window]{.arg} [window]{.optdot}
: Removes each of the *window*s from grid for its container and unmaps their windows. The content will no longer be managed by the grid geometry manager. However, the configuration options for that window are remembered, so that if the content window is managed once more by the grid geometry manager, the previous values are retained.

    [If the last content window of the container becomes unmanaged, this will also send the virtual event **<<NoManagedChild>>** to the container; the container may choose to resize itself (or otherwise respond) to such a change.]{version="TIP 518"}

[grid]{.cmd} [size]{.sub} [container]{.arg}
: Returns the size of the grid (in columns then rows) for *container*. The size is determined either by the *content* occupying the largest row or column, or the largest column or row with a **-minsize**, **-weight**, or **-pad** that is non-zero.

[grid]{.cmd} [slaves]{.sub} [window]{.arg} [-option value]{.optarg}
: Synonym for **grid content** *window* ?*-option value*?.


# Relative placement

The **grid** command contains a limited set of capabilities that permit layouts to be created without specifying the row and column information for each content.  This permits content to be rearranged, added, or removed without the need to explicitly specify row and column information. When no column or row information is specified for a *content*, default values are chosen for **-column**, **-row**, **-columnspan** and **-rowspan** at the time the *content* is managed. The values are chosen based upon the current layout of the grid, the position of the *content* relative to other *content*s in the same grid command, and the presence of the characters **-**, **x**, and **^** in **grid** command where *content* names are normally expected.

**-**
: This increases the **-columnspan** of the *content* to the left.  Several **-**'s in a row will successively increase the number of columns spanned. A **-** may not follow a **^** or a **x**, nor may it be the first *content* argument to **grid configure**.

**x**
: This leaves an empty column between the *content* on the left and the *content* on the right.

**^**
: This extends the **-rowspan** of the *content* above the **^**'s in the grid.  The number of **^**'s in a row must match the number of columns spanned by the *content* above it.


# The grid algorithm

The grid geometry manager lays out its content in three steps. In the first step, the minimum size needed to fit all of the content is computed, then (if propagation is turned on), a request is made of the container window to become that size. In the second step, the requested size is compared against the actual size of the container.  If the sizes are different, then spaces is added to or taken away from the layout as needed. For the final step, each content is positioned in its row(s) and column(s) based on the setting of its *sticky* flag.

To compute the minimum size of a layout, the grid geometry manager first looks at all content whose **-columnspan** and **-rowspan** values are one, and computes the nominal size of each row or column to be either the *minsize* for that row or column, or the sum of the *pad*ding plus the size of the largest content, whichever is greater.  After that the rows or columns in each uniform group adapt to each other.  Then the content whose row-spans or column-spans are greater than one are examined.  If a group of rows or columns need to be increased in size in order to accommodate these content, then extra space is added to each row or column in the group according to its *weight*.  For each group whose weights are all zero, the additional space is apportioned equally.

When multiple rows or columns belong to a uniform group, the space allocated to them is always in proportion to their weights. (A weight of zero is considered to be 1.)  In other words, a row or column configured with **-weight 1 -uniform a** will have exactly the same size as any other row or column configured with **-weight 1 -uniform a**.  A row or column configured with **-weight 2 -uniform b** will be exactly twice as large as one that is configured with **-weight 1 -uniform b**.

More technically, each row or column in the group will have a size equal to *k\*weight* for some constant *k*.  The constant *k* is chosen so that no row or column becomes smaller than its minimum size.  For example, if all rows or columns in a group have the same weight, then each row or column will have the same size as the largest row or column in the group.

For containers whose size is larger than the requested layout, the additional space is apportioned according to the row and column weights.  If all of the weights are zero, the layout is placed within its container according to the *anchor* value. For containers whose size is smaller than the requested layout, space is taken away from columns and rows according to their weights.  However, once a column or row shrinks to its minsize, its weight is taken to be zero. If more space needs to be removed from a layout than would be permitted, as when all the rows or columns are at their minimum sizes, the layout is placed and clipped according to the *anchor* value.

# Geometry propagation

The grid geometry manager normally computes how large a container must be to just exactly meet the needs of its content, and it sets the requested width and height of the container to these dimensions. This causes geometry information to propagate up through a window hierarchy to a top-level window so that the entire sub-tree sizes itself to fit the needs of the leaf windows. However, the **grid propagate** command may be used to turn off propagation for one or more containers. If propagation is disabled then grid will not set the requested width and height of the container window. This may be useful if, for example, you wish for a container window to have a fixed size that you specify.

# Restrictions on container windows

The container for each content must either be the content's parent (the default) or a descendant of the content's parent. This restriction is necessary to guarantee that the content can be placed over any part of its container that is visible without danger of the content being clipped by its parent. In addition, all content in one call to **grid** must have the same container.

# Stacking order

If the container for a content is not its parent then you must make sure that the content is higher in the stacking order than the container. Otherwise the container will obscure the content and it will appear as if the content has not been managed correctly. The easiest way to make sure the content is higher than the container is to create the container window first:  the most recently created window will be highest in the stacking order.

# Credits

The **grid** command is based on ideas taken from the *GridBag* geometry manager written by Doug. Stein, and the **blt\_table** geometry manager, written by George Howlett.

# Examples

A toplevel window containing a text widget and two scrollbars:

```
# Make the widgets
toplevel .t
text .t.txt -wrap none -xscroll {.t.h set} -yscroll {.t.v set}
scrollbar .t.v -orient vertical   -command {.t.txt yview}
scrollbar .t.h -orient horizontal -command {.t.txt xview}

# Lay them out
grid .t.txt .t.v -sticky nsew
grid .t.h        -sticky nsew

# Tell the text widget to take all the extra room
grid rowconfigure    .t .t.txt -weight 1
grid columnconfigure .t .t.txt -weight 1
```

Three widgets of equal width, despite their different "natural" widths:

```
button .b -text "Foo"
entry .e -textvariable foo ; set foo "Hello World!"
label .l -text "This is a fairly long piece of text"

grid .b .e .l -sticky ew
grid columnconfigure . "all" -uniform allTheSame
```


[pack]: pack.md

