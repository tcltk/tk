---
CommandName: ttk::notebook
title: ttk::notebook
ManualSection: n
Version: 8.5
TclPart: Tk
TclDescription: Tk Themed Widget
Links:
 - ttk::widget(n)
 - grid(n)
Keywords:
 - pane
 - tab
Copyright:
 - Copyright (c) 2004 Joe English
---

# Name

ttk::notebook - Multi-paned container widget

# Synopsis

::: {.synopsis} :::
[ttk::notebook]{.cmd} [pathname]{.arg} [options...]{.optarg}
[pathname]{.ins} [add]{.sub} [window]{.arg} [options...]{.optarg}
[pathname]{.ins} [insert]{.sub} [index]{.arg} [window]{.arg} [options...]{.optarg}
:::

# Description

A **ttk::notebook** widget manages a collection of windows and displays a single one at a time. Each content window is associated with a *tab*, which the user may select to change the currently-displayed window.

If a content window is not a child of the notebook, it must be higher in the stacking order than the notebook, otherwise the notebook will obscure it.  The easiest way to ensure this is to create the notebook first: the most recently created window is highest in the stacking order. Or, you can use the [raise] and [lower] commands.

# Standard options

::: {.standardoptions} :::
-class
-cursor
-style
-takefocus
:::

See the [ttk\_widget][ttk_widget] manual entry for details on the standard options.

# Widget-specific options

[-height]{.cmdline} [height]{.dbname} [Height]{.dbclass}
: If present and greater than zero, specifies the desired height of the pane area (not including internal padding or tabs). Otherwise, the maximum height of all panes is used.

[-padding]{.cmdline} [padding]{.dbname} [Padding]{.dbclass}
: Specifies the amount of extra space to add around the outside of the notebook. The padding is a list of up to four length specifications *left top right bottom*. If fewer than four elements are specified, *bottom* defaults to *top*, *right* defaults to *left*, and *top* defaults to *left*. In other words, a list of three numbers specify the left, vertical, and right padding; a list of two numbers specify the horizontal and the vertical padding; a single number specifies the same padding all the way around the widget.

[-width]{.cmdline} [width]{.dbname} [Width]{.dbclass}
: If present and greater than zero, specifies the desired width of the pane area (not including internal padding). Otherwise, the maximum width of all panes is used.


# Tab options

The following options may be specified for individual notebook panes:

[-state]{.cmdline} [state]{.dbname} [State]{.dbclass}
: Either **normal**, **disabled** or **hidden**. If **disabled**, then the tab is not selectable. If **hidden**, then the tab is not shown.

[-sticky]{.cmdline} [sticky]{.dbname} [Sticky]{.dbclass}
: Specifies how the content window is positioned within the pane area. Value is a string containing zero or more of the characters **n, s, e,** or **w**. Each letter refers to a side (north, south, east, or west) that the content window will "stick" to, as per the [grid] geometry manager.

[-padding]{.cmdline} [padding]{.dbname} [Padding]{.dbclass}
: Specifies the amount of extra space to add between the notebook and this pane. Syntax is the same as for the widget **-padding** option.

[-text]{.cmdline} [text]{.dbname} [Text]{.dbclass}
: Specifies a string to be displayed in the tab.

[-image]{.cmdline} [image]{.dbname} [Image]{.dbclass}
: Specifies an image to display in the tab. See *ttk\_widget(n)* for details.

[-compound]{.cmdline} [compound]{.dbname} [Compound]{.dbclass}
: Specifies how to display the image relative to the text, in the case both **-text** and **-image** are present. See *label(n)* for legal values.

[-underline]{.cmdline} [underline]{.dbname} [Underline]{.dbclass}
: Specifies the integer index (0-based) of a character to underline in the text string. The underlined character is used for mnemonic activation if **ttk::notebook::enableTraversal** is called.


# Tab identifiers

The *tabid* argument to the following commands may take any of the following forms:

- An integer between zero and the number of tabs;

- The name of a content window;

- A positional specification of the form "@*x*,*y*", which identifies the tab

- The literal string "**current**", which identifies the currently-selected tab; or:

- The literal string "**end**", which returns the number of tabs (only valid for "*pathname* **index**").


Indexes support the same simple interpretation as for the command [string index][string], with simple integer index arithmetic and indexing relative to **end**.

# Widget command

In addition to the standard **cget**, **configure**, **instate**, **state** and **style** commands (see [ttk::widget][ttk_widget]), notebook widgets support the following additional commands:

[pathname]{.ins} [add]{.sub} [window]{.arg} [options...]{.optarg}
: Adds a new tab to the notebook. See [Tab options] for the list of available *options*. If *window* is currently managed by the notebook but hidden, it is restored to its previous position.

[pathname]{.ins} [forget]{.sub} [tabid]{.arg}
: Removes the tab specified by *tabid*, unmaps and unmanages the associated window.

[pathname]{.ins} [hide]{.sub} [tabid]{.arg}
: Hides the tab specified by *tabid*. The tab will not be displayed, but the associated window remains managed by the notebook and its configuration remembered. Hidden tabs may be restored with the **add** command.

[pathname]{.ins} [identify]{.sub} [component]{.arg} [x]{.arg} [y]{.arg}
: Returns the name of the element under the point given by *x* and *y*, or the empty string if no component is present at that location. The following subcommands are supported:

    [pathname]{.ins} [identify]{.sub} [element]{.lit} [x]{.arg} [y]{.arg}
    : Returns the name of the element at the specified location.

    [pathname]{.ins} [identify]{.sub} [tab]{.lit} [x]{.arg} [y]{.arg}
    : Returns the index of the tab at the specified location.


[pathname]{.ins} [index]{.sub} [tabid]{.arg}
: Returns the numeric index of the tab specified by *tabid*, or the total number of tabs if *tabid* is the string "**end**".

[pathname]{.ins} [insert]{.sub} [pos]{.arg} [subwindow]{.arg} [options...]{.arg}
: Inserts a pane at the specified position. *pos* is either the string **end**, an integer index, or the name of a managed subwindow. If *subwindow* is already managed by the notebook, moves it to the specified position. See [Tab options] for the list of available options.

[pathname]{.ins} [select]{.sub} [tabid]{.optarg}
: Selects the specified tab. The associated content window will be displayed, and the previously-selected window (if different) is unmapped. If *tabid* is omitted, returns the widget name of the currently selected pane.

[pathname]{.ins} [tab]{.sub} [tabid]{.arg}
: see below ...

[pathname]{.ins} [tab]{.sub} [tabid]{.arg} [-option]{.arg}
: see below ...

[pathname]{.ins} [tab]{.sub} [tabid]{.arg} [-option]{.arg} [value]{.arg} [-option value]{.optdot}
: Query or modify the options of the specific tab. In its first form, when no *-option* is specified, the command returns a dictionary of the tab options and their values. The second form, when one *-option* is specified, returns the value of that *option*. Otherwise, in its third form, it sets the *-option*s to the corresponding *value*s. See [Tab options] for the available options.

[pathname]{.ins} [tabs]{.sub}
: Returns the list of windows managed by the notebook, in the index order of their associated tabs.


# Keyboard traversal

To enable keyboard traversal for a toplevel window containing a notebook widget *$nb*, call:

```
ttk::notebook::enableTraversal $nb
```

This will extend the bindings for the toplevel window containing the notebook as follows:

- **Control-Tab** selects the tab following the currently selected one.

- **Control-Shift-Tab** selects the tab preceding the currently selected one.

- **Alt-***K*, where *K* is the mnemonic (underlined) character of any tab, will select that tab.


Multiple notebooks in a single toplevel may be enabled for traversal, including nested notebooks. However, notebook traversal only works properly if all panes are direct children of the notebook.

# Virtual events

The notebook widget generates a **<<NotebookTabChanged>>** virtual event after a new tab is selected.

# Example

```
pack [ttk::notebook .nb]
.nb add [frame .nb.f1] -text "First tab"
.nb add [frame .nb.f2] -text "Second tab"
.nb select .nb.f2
ttk::notebook::enableTraversal .nb
```

# Styling options

The class name for a **ttk::notebook** is **TNotebook**.  The tab has a class name of **TNotebook.Tab**

Dynamic states: **active**, **disabled**, **selected**.

**TNotebook** styling options configurable with [ttk::style][ttk_style] are:

**-background** *color* .br **-bordercolor** *color* .br **-darkcolor** *color* .br **-foreground** *color* .br **-lightcolor** *color* .br **-padding** *padding* .br **-tabmargins** *padding* .br **-tabposition** *position* Specifies the position of the tab row or column as a string of length 1 or 2.  The first character indicates the side as **n**, **s**, **w**, or **e**, while the second character (if present) is the sticky bit (specified as **w**, **e**, **n**, or **s**) within the tab position.  The default position is **n** for the **aqua** theme and **nw** for all the other built-in themes. .br **-tabplacement** *placement* Specifies the placement (anchor) of the tabs within the tab row or column as a string of length 1 or 2.  The first character indicates the side as **w**, **e**, **n**, or **s**, while the second character (if present) is the sticky bit (specified as **s**, **n**, **e**, or **w**) within the tab placement.  For example, the tab placement **ws** means: attach the west (left) side of each tab to the previous one and align the south (bottom) edges with each other.  Similarly, the tab placement **se** specifies to attach the south (bottom) side of each tab to the previous one and align the east (right) edges with each other.  The default placement depends on the first character of the tab position: **ws** for **n\***, **wn** for **s\***, **ne** for **w\***, and **nw** for **e\***. .br

**TNotebook.Tab** styling options configurable with [ttk::style][ttk_style] are:

**-background** *color* .br **-bordercolor** *color* .br **-compound** *compound* .br **-expand** *padding* Defines how much the tab grows in size.  Usually used with the **selected** dynamic state.  **-tabmargins** should be set appropriately so that there is room for the tab growth. For example, the Ttk library file **vistaTheme.tcl** contains the lines

```
ttk::style configure TNotebook -tabmargins {2 2 2 0}
ttk::style map TNotebook.Tab -expand {selected {2 2 2 2}}
```

which are valid for the default value **nw** of the **-tabposition** style option.  For a **ttk::notebook** style **nbStyle** defined by

```
set nbStyle SW.TNotebook
ttk::style configure $nbStyle -tabposition sw
```

you will have to adapt the above settings as follows:

```
ttk::style configure $nbStyle -tabmargins {2 0 2 2}
ttk::style map $nbStyle.Tab -expand {selected {2 2 2 2}}
```

The easiest way to do this is to invoke the library procedure **ttk::configureNotebookStyle** with **$nbStyle** as argument, after setting the style's **-tabposition** option. **-font** *font* .br **-foreground** *color* .br **-padding** *padding* Some themes use a different *padding* for the selected tab.  For example, the Ttk library file **clamTheme.tcl** contains the lines

```
ttk::style configure TNotebook.Tab \
    -padding {4.5p 1.5p 4.5p 1.5p}
ttk::style map TNotebook.Tab \
    -padding {selected {4.5p 3p 4.5p 1.5p}}
```

which are valid for the default value **nw** of the **-tabposition** style option.  For a **ttk::notebook** style having a different tab position you will have to adapt the above settings accordingly.  Again, the easiest way to do this is to invoke the library procedure **ttk::configureNotebookStyle** with the style name as argument, after setting the style's **-tabposition** option. **-textangle** *Angle* .br

Some options are only available for specific themes.

See the [ttk::style][ttk_style] manual page for information on how to configure ttk styles.


[grid]: grid.md
[lower]: lower.md
[raise]: raise.md
[string]: ../Tcl/string.md
[ttk_style]: ttk_style.md
[ttk_widget]: ttk_widget.md

