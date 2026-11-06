---
CommandName: ttk::widget
title: ttk::widget
ManualSection: n
Version: 8.5
TclPart: Tk
TclDescription: Tk Themed Widget
Links:
 - ttk::intro(n)
 - ttk::style(n)
Keywords:
 - state
 - configure
 - option
Copyright:
 - Copyright (c) 2004 Joe English
---

# Name

ttk::widget - Standard options and commands supported by Tk themed widgets

# Description

This manual describes common widget options and commands.

# Standard options

The following options are supported by all Tk themed widgets:

[-class]{.cmdline} [undefined]{.dbname} [undefined]{.dbclass}
: Specifies the window class. The class is used when querying the option database for the window's other options, to determine the default bindtags for the window, and to select the widget's default layout and style. This is a read-only option: it may only be specified when the window is created, and may not be changed with the **configure** widget command.

[-cursor]{.cmdline} [cursor]{.dbname} [Cursor]{.dbclass}
: Specifies the mouse cursor to be used for the widget. See [Tk\_GetCursor][GetCursor] and *cursors(n)* in the Tk reference manual for the legal values. If set to the empty string (the default), the cursor is inherited from the parent widget.

[-takefocus]{.cmdline} [takeFocus]{.dbname} [TakeFocus]{.dbclass}
: Determines whether the window accepts the focus during keyboard traversal. Either **0**, **1**, a command prefix (to which the widget path is appended, and which should return **0** or **1**), or the empty string. See *options(n)* in the Tk reference manual for the full description.

[-style]{.cmdline} [style]{.dbname} [Style]{.dbclass}
: May be used to specify a custom widget style.


# Scrollable widget options

The following options are supported by widgets that are controllable by a scrollbar. See *scrollbar(n)* for more information

[-xscrollcommand]{.cmdline} [xScrollCommand]{.dbname} [ScrollCommand]{.dbclass}
: A command prefix, used to communicate with horizontal scrollbars. When the view in the widget's window changes, the widget will generate a Tcl command by concatenating the scroll command and two numbers. Each of the numbers is a fraction between 0 and 1 indicating a position in the document; 0 indicates the beginning, and 1 indicates the end. The first fraction indicates the first information in the widget that is visible in the window, and the second fraction indicates the information just after the last portion that is visible.

    Typically the **-xscrollcommand** option consists of the path name of a [scrollbar] widget followed by "set", e.g. ".x.scrollbar set". This will cause the scrollbar to be updated whenever the view in the window changes.

    If this option is set to the empty string (the default), then no command will be executed.

[-yscrollcommand]{.cmdline} [yScrollCommand]{.dbname} [ScrollCommand]{.dbclass}
: A command prefix, used to communicate with vertical scrollbars. See the description of **-xscrollcommand** above for details.


# Label options

The following options are supported by labels, buttons, and other button-like widgets:

[-anchor]{.cmdline} [anchor]{.dbname} [Anchor]{.dbclass}
: Specifies how the information in the widget is positioned relative to the inner margins.  Legal values are **n**, **ne**, **e**, **se**, **s**, **sw**, **w**, **nw**, and **center**. If unspecified, the theme default is used. See also **-justify** (for widgets supporting this option).

[-compound]{.cmdline} [compound]{.dbname} [Compound]{.dbclass}
: Specifies how to display the image relative to the text, in the case both **-text** and **-image** are present. If set to the empty string (the default), the rules described in the "Elements" section of *ttk::intro(n)* explain which value is actually used. The other valid values are:

    text
    : Display text only.

    image
    : Display image only.

    center
    : Display text centered on top of image.

    top
    : see below ...

    bottom
    : see below ...

    left
    : see below ...

    right
    : Display image above, below, left of, or right of the text, respectively.

    none
    : Display the image if present, otherwise the text.


[-font]{.cmdline} [font]{.dbname} [Font]{.dbclass}
: Font to use for the text displayed by the widget.

[-foreground]{.cmdline} [textColor]{.dbname} [TextColor]{.dbclass}
: The widget's foreground color. If unspecified, the theme default is used.

[-image]{.cmdline} [image]{.dbname} [Image]{.dbclass}
: Specifies an image to display. This is a list of 1 or more elements. The first element is the default image name. The rest of the list is a sequence of *statespec / value* pairs as per **style map**, specifying different images to use when the widget is in a particular state or combination of states. All images in the list should have the same size.

[-justify]{.cmdline} [justify]{.dbname} [Justify]{.dbclass}
: If there are multiple lines of text, specifies how the lines are laid out relative to one another. One of **left**, **center**, or **right**. If unspecified, the theme default is used. See also **-anchor** (for widgets supporting this option).

[-locale]{.cmdline} [locale]{.dbname} [Locale]{.dbclass}
: The locale which is used for determining word and character boundaries within the text. The default is "C".

[-padding]{.cmdline} [padding]{.dbname} [Padding]{.dbclass}
: Specifies the internal padding for the widget. The padding is a list of up to four length specifications *left top right bottom*. If fewer than four elements are specified, *bottom* defaults to *top*, *right* defaults to *left*, and *top* defaults to *left*. In other words, a list of three numbers specify the left, vertical, and right padding; a list of two numbers specify the horizontal and the vertical padding; a single number specifies the same padding all the way around the widget.

[-text]{.cmdline} [text]{.dbname} [Text]{.dbclass}
: Specifies a text string to be displayed inside the widget (unless overridden by **-textvariable** for the widgets supporting this option).

[-textangle]{.cmdline} [textangle]{.dbname} [Angle]{.dbclass}
: Tells how many degrees to rotate the text anticlockwise about the positioning point for the text; it may have any floating-point value from 0.0 to 360.0. For example, if the parameter is **90**, then the text will be drawn vertically from bottom to top. This option defaults to the empty string which is 0 degrees.

[-textvariable]{.cmdline} [textVariable]{.dbname} [Variable]{.dbclass}
: Specifies the name of a global variable whose value will be used in place of the **-text** resource.

[-underline]{.cmdline} [underline]{.dbname} [Underline]{.dbclass}
: If set, specifies the integer index (0-based) of a character to underline in the text string. **end** corresponds to the last character, **end**-1 to the before last character, and so on. The underlined character is used for mnemonic activation.

[-width]{.cmdline} [width]{.dbname} [Width]{.dbclass}
: If greater than zero, specifies how much space, in character widths, to allocate for the text label. If less than zero, specifies a minimum width. If zero or unspecified, the natural width of the text label is used. Note that some themes may specify a non-zero **-width** in the style.

[-wraplength]{.cmdline} [wrapLength]{.dbname} [WrapLength]{.dbclass}
: Specifies the maximum line length. The value may have any of the forms acceptable to [Tk\_GetPixels][GetPixels]. If this option is less than or equal to zero, then automatic wrapping is not performed; otherwise the text is split into lines such that no line is longer than the specified value.


# Entry options

The following options are supported by entry, spinbox and combobox:

[-placeholder]{.cmdline} [placeHolder]{.dbname} [PlaceHolder]{.dbclass}
: Specifies a help text string to display if no text is otherwise displayed, that is when the widget is empty. The placeholder text is displayed using the values of the **-font**, **-justify** and **-placeholderforeground** options.

[-placeholderforeground]{.cmdline} [placeHolderForeground]{.dbname} [PlaceHolderForeground]{.dbclass}
: Specifies the foreground color of the placeholder text.


# Compatibility options

This option is only available for themed widgets that have "corresponding" traditional Tk widgets.

[-state]{.cmdline} [state]{.dbname} [State]{.dbclass}
: May be set to **normal** or **disabled** to control the **disabled** state bit. This is a write-only option: setting it changes the widget state, but the **state** widget command does not affect the **-state** option.


# Commands

[pathName]{.ins} [cget]{.sub} [option]{.arg}
: Returns the current value of the configuration option given by *option*.

[pathName]{.ins} [configure]{.sub} [option]{.optarg} [value option value]{.optdot}
: Query or modify the configuration options of the widget. If one or more *option-value* pairs are specified, then the command modifies the given widget option(s) to have the given value(s); in this case the command returns an empty string. If *option* is specified with no *value*, then the command returns a list describing the named option: the elements of the list are the option name, database name, database class, default value, and current value. If no *option* is specified, returns a list describing all of the available options for *pathName*.

[pathName]{.ins} [identify]{.sub} [element]{.lit} [x]{.arg} [y]{.arg}
: Returns the name of the element under the point given by *x* and *y*, or an empty string if the point does not lie within any element. *x* and *y* are pixel coordinates relative to the widget. Some widgets accept other **identify** subcommands described in these widgets documentation.

[pathName]{.ins} [instate]{.sub} [statespec]{.arg} [script]{.optarg}
: Test the widget's state. If *script* is not specified, returns 1 if the widget state matches *statespec* and 0 otherwise. If *script* is specified, equivalent to

    ```
    if {[pathName instate stateSpec]} script
    ```

[pathName]{.ins} [state]{.sub} [stateSpec]{.optarg}
: Modify or inquire widget state. If *stateSpec* is present, sets the widget state: for each flag in *stateSpec*, sets the corresponding flag or clears it if prefixed by an exclamation point. Returns a new state spec indicating which flags were changed:

    ```
    set changes [pathName state spec]
    pathName state $changes
    ```

    will restore *pathName* to the original state. If *stateSpec* is not specified, returns a list of the currently-enabled state flags.

[pathName]{.ins} [style]{.sub}
: Return the style used by the widget.

[pathName]{.ins} [xview]{.sub} [args]{.arg}
: This command is used to query and change the horizontal position of the content in the widget's window.  It can take any of the following forms:

    [pathName]{.ins} [xview]{.sub}
    : Returns a list containing two elements. Each element is a real fraction between 0 and 1; together they describe the horizontal span that is visible in the window. For example, if the first element is .2 and the second element is .6, 20% of the widget's content is off-screen to the left, the middle 40% is visible in the window, and 40% of the content is off-screen to the right. These are the same values passed to scrollbars via the **-xscrollcommand** option.

    [pathName]{.ins} [xview]{.sub} [index]{.arg}
    : Adjusts the view in the window so that the content given by *index* is displayed at the left edge of the window.

    [pathName]{.ins} [xview]{.sub} [moveto]{.lit} [fraction]{.arg}
    : Adjusts the view in the window so that the character *fraction* of the way through the content appears at the left edge of the window. *Fraction* must be a fraction between 0 and 1.

    [pathName]{.ins} [xview]{.sub} [scroll]{.lit} [number]{.arg} [what]{.arg}
    : This command shifts the view in the window left or right according to *number* and *what*. *Number* must be an integer or a float, but if it is a float then it is converted to an integer, rounded away from 0. *What* must be either **pages** or **units**. If *what* is **pages** then the view adjusts by *number* screenfuls. If *number* is negative then characters farther to the left become visible;  if it is positive then characters farther to the right become visible. If *what* is **units**, the view adjusts left or right by *number* average-width characters on the display.


[pathName]{.ins} [yview]{.sub} [args]{.arg}
: This command is used to query and change the vertical position of the content in the widget's window.  It can take any of the following forms:

    [pathName]{.ins} [yview]{.sub}
    : Returns a list containing two elements. Each element is a real fraction between 0 and 1; together they describe the vertical span that is visible in the window. For example, if the first element is .2 and the second element is .6, 20% of the widget's content is off-screen to the top, the middle 40% is visible in the window, and 40% of the content is off-screen to the bottom. These are the same values passed to scrollbars via the **-yscrollcommand** option.

    [pathName]{.ins} [yview]{.sub} [index]{.arg}
    : Adjusts the view in the window so that the content given by *index* is displayed at the top edge of the window.

    [pathName]{.ins} [yview]{.sub} [moveto]{.lit} [fraction]{.arg}
    : Adjusts the view in the window so that the item *fraction* of the way through the content appears at the top edge of the window. *Fraction* must be a fraction between 0 and 1.

    [pathName]{.ins} [yview]{.sub} [scroll]{.lit} [number]{.arg} [what]{.arg}
    : This command shifts the view in the window up or down according to *number* and *what*. *Number* must be an integer or a float, but if it is a float then it is converted to an integer, rounded away from 0. *What* must be either **pages** or **units**. If *what* is **pages** then the view adjusts by *number* screenfuls. If *number* is negative then items farther to the top become visible;  if it is positive then items farther to the bottom become visible. If *what* is **units**, the view adjusts up or down by *number* average-width characters on the display.



# Widget states

The widget state is a bitmap of independent state flags. Widget state flags include:

**active**
: The mouse cursor is over the widget and pressing a mouse button will cause some action to occur. (aka "prelight" (Gnome), "hot" (Windows), "hover").

**alternate**
: A widget-specific alternate display format. For example, used for checkbuttons and radiobuttons in the "tristate" or "mixed" state, and for buttons with **-default active**.

**background**
: Windows and the Mac have a notion of an "active" or foreground window. The **background** state is set for widgets in a background window, and cleared for those in the foreground window.

**disabled**
: Widget is disabled under program control (aka "unavailable", "inactive").

**first**
: Widget is the first one in a container. Used for tab elements in a notebook.

[focus]
: Widget has keyboard focus.

**hover**
: The mouse cursor is within the widget. This is similar to the **active** state; it is used in some themes for widgets that provide distinct visual feedback for the active widget in addition to the active element within the widget.

**invalid**
: The widget's value is invalid. (Potential uses: scale widget value out of bounds, entry widget value failed validation.)

**last**
: Widget is the last one in a container. Used for tab elements in a notebook.

**leaf**
: Widget is a leaf. Used in a treeview widget.

[open]
: Widget is in the open state. Used in a treeview widget.

**pressed**
: Widget is being pressed (aka "armed" in Motif).

**readonly**
: Widget should not allow user modification.

**selected**
: "On", "true", or "current" for things like checkbuttons and radiobuttons.

**user1**-**user6**
: Freely usable for other purposes


A *state specification* or *stateSpec* is a list of state names, optionally prefixed with an exclamation point (!) indicating that the bit is off.

# Examples

```
set b [ttk::button .b]

# Disable the widget:
$b state disabled

# Invoke the widget only if it is currently pressed and enabled:
$b instate {pressed !disabled} { .b invoke }

# Reenable widget:
$b state !disabled
```


[focus]: focus.md
[GetCursor]: ../TkCAPI/GetCursor.md
[GetPixels]: ../TkCAPI/GetPixels.md
[open]: ../Tcl/open.md
[scrollbar]: scrollbar.md

