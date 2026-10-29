---
CommandName: ttk::combobox
title: ttk::combobox
ManualSection: n
Version: 8.5
TclPart: Tk
TclDescription: Tk Themed Widget
Links:
 - ttk::widget(n)
 - ttk::entry(n)
Keywords:
 - choice
 - entry
 - list box
 - text box
 - widget
Copyright:
 - Copyright (c) 2004 Joe English
---

# Name

ttk::combobox - text field with popdown selection list

# Synopsis

::: {.synopsis} :::
[ttk::combobox]{.cmd} [pathName]{.arg} [options]{.optarg}
:::

# Description

A **ttk::combobox** combines a text field with a pop-down list of values; the user may select the value of the text field from among the values in the list.

# Standard options

::: {.standardoptions} :::
-class
-cursor
-placeholder
-placeholderforeground
-style
-takefocus
:::

See the [ttk\_widget][ttk_widget] manual entry for details on the standard options.

# Widget-specific options

[-exportselection]{.cmdline} [exportSelection]{.dbname} [ExportSelection]{.dbclass}
: Boolean value. If set, the widget selection is linked to the X selection.

[-justify]{.cmdline} [justify]{.dbname} [Justify]{.dbclass}
: Specifies how the text is aligned within the widget. Must be one of **left**, **center**, or **right**.

[-locale]{.cmdline} [locale]{.dbname} [Locale]{.dbclass}
: The locale which is used for determining word and character boundaries within the text. The default is "C".

[-height]{.cmdline} [height]{.dbname} [Height]{.dbclass}
: Specifies the height of the pop-down listbox, in rows.

[-postcommand]{.cmdline} [postCommand]{.dbname} [PostCommand]{.dbclass}
: A Tcl script to evaluate immediately before displaying the listbox. The **-postcommand** script may specify the **-values** to display.

[-state]{.cmdline} [state]{.dbname} [State]{.dbclass}
: One of **normal**, **readonly**, or **disabled**. In the **readonly** state, the value may not be edited directly, and the user can only select one of the **-values** from the dropdown list. In the **normal** state, the text field is directly editable. In the **disabled** state, no interaction is possible.

[-textvariable]{.cmdline} [textVariable]{.dbname} [TextVariable]{.dbclass}
: Specifies the name of a global variable whose value is linked to the widget value. Whenever the variable changes value the widget value is updated, and vice versa.

[-values]{.cmdline} [values]{.dbname} [Values]{.dbclass}
: Specifies the list of values to display in the drop-down listbox.

[-width]{.cmdline} [width]{.dbname} [Width]{.dbclass}
: Specifies an integer value indicating the desired width of the entry window, in average-size characters of the widget's font.


# Widget command

In addition to the standard **cget**, **configure**, **identify element**, **instate**, **state** and **style** commands (see [ttk::widget][ttk_widget]), combobox widgets support the following additional commands:

[pathName]{.ins} [current]{.sub} [newIndex]{.optarg}
: If *newIndex* is supplied, sets the combobox value to the element at position *newIndex* in the list of **-values** (in addition to integers, the **end** index is supported and indicates the last element of the list, moreover the same simple interpretation as for the command [string index][string] is supported, with simple integer index arithmetic and indexing relative to **end**). Otherwise, returns the index of the current value in the list of **-values** or **{}** if the current value does not appear in the list.

[pathName]{.ins} [get]{.sub}
: Returns the current value of the combobox.

[pathName]{.ins} [set]{.sub} [value]{.arg}
: Sets the value of the combobox to *value*.


The combobox widget also supports the following [ttk::entry][ttk_entry] widget commands:

::: {.info DISPLAY="yes"}
**bbox**	**delete**	**icursor** **index**	**insert**	**selection** **xview**
:::

# Virtual events

The combobox widget generates a **<<ComboboxSelected>>** virtual event when the user selects an element from the list of values. If the selection action unposts the listbox, this event is delivered after the listbox is unposted.

# Styling options

The class name for a **ttk::combobox** is **TCombobox**. The **ttk::combobox** uses the [entry] and [listbox] widgets internally. The listbox frame has a class name of **ComboboxPopdownFrame**.

Dynamic states: **disabled**, [focus], **pressed**, **readonly**.

**TCombobox** styling options configurable with [ttk::style][ttk_style] are:

**-arrowcolor** *color* .br **-arrowsize** *amount* .br **-background** *color* .br **-bordercolor** *color* .br **-darkcolor** *color* .br **-focusfill** *color* .br **-foreground** *color* .br **-fieldbackground** *color* Can only be changed when using non-native and non-graphical themes. **-insertcolor** *color* .br **-insertwidth** *amount* .br **-lightcolor** *color* .br **-padding** *padding* .br **-placeholderforeground** *color* .br **-postoffset** *padding* .br **-selectbackground** *color* Text entry select background.  If empty, the selected text has no distinct background. **-selectforeground** *color* Text entry select foreground.  If empty, the **-foreground** color is used.

The **ttk::combobox** popdown listbox cannot be configured using [ttk::style][ttk_style] nor via the widget **configure** command.  The listbox can be configured using the option database.

option add \*TCombobox\*Listbox.background *color* .br option add \*TCombobox\*Listbox.font *font* .br option add \*TCombobox\*Listbox.foreground *color* .br option add \*TCombobox\*Listbox.selectBackground *color* .br option add \*TCombobox\*Listbox.selectForeground *color*

To configure a specific listbox (subject to future change):

```
set popdown [ttk::combobox::PopdownWindow .mycombobox]
$popdown.f.l configure -font font
```

**ComboboxPopdownFrame** styling options configurable with [ttk::style][ttk_style] are:

**-borderwidth** *amount* .br **-relief** *relief*

Some options are only available for specific themes.

See the [ttk::style][ttk_style] manual page for information on how to configure ttk styles.


[entry]: entry.md
[focus]: focus.md
[listbox]: listbox.md
[string]: ../Tcl/string.md
[ttk_entry]: ttk_entry.md
[ttk_style]: ttk_style.md
[ttk_widget]: ttk_widget.md

