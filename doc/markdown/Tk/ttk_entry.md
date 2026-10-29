---
CommandName: ttk::entry
title: ttk::entry
ManualSection: n
Version: 8.5
TclPart: Tk
TclDescription: Tk Themed Widget
Links:
 - ttk::widget(n)
 - entry(n)
Keywords:
 - entry
 - widget
 - text field
Copyright:
 - Copyright (c) 1994-1996 Sun Microsystems, Inc.
 - Copyright (c) 1998-2000 Scriptics Corporation.
 - Copyright (c) 2004 Joe English
---

# Name

ttk::entry - Editable text field widget

# Synopsis

::: {.synopsis} :::
[ttk::entry]{.cmd} [pathName]{.arg} [options]{.optarg}
:::

# Description

A **ttk::entry** widget displays a one-line text string and allows that string to be edited by the user. The value of the string may be linked to a Tcl variable with the **-textvariable** option. Entry widgets support horizontal scrolling with the standard **-xscrollcommand** option and **xview** widget command.

# Standard options

::: {.standardoptions} :::
-class
-cursor
-font
-foreground
-placeholder
-placeholderforeground
-style
-takefocus
-xscrollcommand
:::

See the [ttk\_widget][ttk_widget] manual entry for details on the standard options.

# Widget-specific options

[-exportselection]{.cmdline} [exportSelection]{.dbname} [ExportSelection]{.dbclass}
: A boolean value specifying whether or not a selection in the widget should be linked to the X selection. If the selection is exported, then selecting in the widget deselects the current X selection, selecting outside the widget deselects any widget selection, and the widget will respond to selection retrieval requests when it has a selection.

[-invalidcommand]{.cmdline} [invalidCommand]{.dbname} [InvalidCommand]{.dbclass}
: A script template to evaluate whenever the **-validatecommand** returns 0. See [Validation] below for more information.

[-justify]{.cmdline} [justify]{.dbname} [Justify]{.dbclass}
: Specifies how the text is aligned within the entry widget. One of **left**, **center**, or **right**.

[-locale]{.cmdline} [locale]{.dbname} [Locale]{.dbclass}
: The locale which is used for determining word and character boundaries within the text. The default is "C".

[-show]{.cmdline} [show]{.dbname} [Show]{.dbclass}
: If this option is specified, then the true contents of the entry are not displayed in the window. Instead, each character in the entry's value will be displayed as the first character in the value of this option, such as "\*" or a bullet. This is useful, for example, if the entry is to be used to enter a password. If characters in the entry are selected and copied elsewhere, the information copied will be what is displayed, not the true contents of the entry.

[-state]{.cmdline} [state]{.dbname} [State]{.dbclass}
: Compatibility option; see *ttk::widget(n)* for details. Specifies one of three states for the entry, **normal**, **disabled**, or **readonly**. See [Widget states], below.

[-textvariable]{.cmdline} [textVariable]{.dbname} [Variable]{.dbclass}
: Specifies the name of a global variable whose value is linked to the entry widget's contents. Whenever the variable changes value, the widget's contents are updated, and vice versa.

[-validate]{.cmdline} [validate]{.dbname} [Validate]{.dbclass}
: Specifies the mode in which validation should operate: **none**, **focus**, **focusin**, **focusout**, **key**, or **all**. Default is **none**, meaning that validation is disabled. See [Validation] below.

[-validatecommand]{.cmdline} [validateCommand]{.dbname} [ValidateCommand]{.dbclass}
: A script template to evaluate whenever validation is triggered. If set to the empty string (the default), validation is disabled. The script must return a boolean value. See [Validation] below.

[-width]{.cmdline} [width]{.dbname} [Width]{.dbclass}
: Specifies an integer value indicating the desired width of the entry window, in average-size characters of the widget's font.


# Notes

A portion of the entry may be selected as described below. If an entry is exporting its selection (see the **-exportselection** option), then it will observe the standard X11 protocols for handling the selection;  entry selections are available as type **STRING**. Entries also observe the standard Tk rules for dealing with the input focus.  When an entry has the input focus it displays an *insert cursor* to indicate where new characters will be inserted.

Entries are capable of displaying strings that are too long to fit entirely within the widget's window.  In this case, only a portion of the string will be displayed;  commands described below may be used to change the view in the window.  Entries use the standard **-xscrollcommand** mechanism for interacting with scrollbars (see the description of the **-xscrollcommand** option for details).

# Indices

Many of the [entry] widget commands take one or more indices as arguments.  An index specifies a particular character in the entry's string, in any of the following ways:

*number*
: Specifies the character as a numerical index, where 0 corresponds to the first character in the string.

**@***number*
: In this form, *number* is treated as an x-coordinate in the entry's window;  the character spanning that x-coordinate is used. For example, "**@0**" indicates the left-most character in the window.

**end**
: Indicates the character just after the last one in the entry's string. This is equivalent to specifying a numerical index equal to the length of the entry's string.

**insert**
: Indicates the character adjacent to and immediately following the insert cursor.

**sel.first**
: Indicates the first character in the selection.  It is an error to use this form if the selection is not in the entry window.

**sel.last**
: Indicates the character just after the last one in the selection. It is an error to use this form if the selection is not in the entry window.


Abbreviations may be used for any of the forms above, e.g. "**e**" or "**sel.l**". In general, out-of-range indices are automatically rounded to the nearest legal value. Indexes support the same simple interpretation as for the command [string index][string], with simple integer index arithmetic and indexing relative to **end**.

# Widget command

In addition to the standard **cget**, **configure**, **identify element**, **instate**, **state**, **style** and **xview** commands (see [ttk::widget][ttk_widget]), entry widgets support the following additional commands:

[pathName]{.ins} [bbox]{.sub} [index]{.arg}
: Returns a list of four numbers describing the bounding box of the character given by *index*. The first two elements of the list give the x and y coordinates of the upper-left corner of the screen area covered by the character (in pixels relative to the widget) and the last two elements give the width and height of the character, in pixels. The bounding box may refer to a region outside the visible area of the window.

[pathName]{.ins} [delete]{.sub} [first]{.arg} [last]{.optarg}
: Delete one or more elements of the entry. *First* is the index of the first character to delete, and *last* is the index of the character just after the last one to delete. If *last* is not specified it defaults to *first*+1, i.e. a single character is deleted. This command returns the empty string.

[pathName]{.ins} [get]{.sub}
: Returns the entry's string.

[pathName]{.ins} [icursor]{.sub} [index]{.arg}
: Arrange for the insert cursor to be displayed just before the character given by *index*.  Returns the empty string.

[pathName]{.ins} [index]{.sub} [index]{.arg}
: Returns the numerical index corresponding to *index*.

[pathName]{.ins} [insert]{.sub} [index]{.arg} [string]{.arg}
: Insert *string* just before the character indicated by *index*.  Returns the empty string.

[pathName]{.ins} [selection]{.sub} [option]{.arg} [arg]{.arg}
: This command is used to adjust the selection within an entry.  It has several forms, depending on *option*:

    [pathName]{.ins} [selection]{.sub} [clear]{.lit}
    : Clear the selection if it is currently in this widget. If the selection is not in this widget then the command has no effect. Returns the empty string.

    *pathName* [selection present][selection]
    : Returns 1 if there is are characters selected in the entry, 0 if nothing is selected.

    *pathName* [selection range][selection] *start end*
    : Sets the selection to include the characters starting with the one indexed by *start* and ending with the one just before *end*. If *end* refers to the same character as *start* or an earlier one, then the entry's selection is cleared.


*pathName* **validate**
: Force revalidation, independent of the conditions specified by the **-validate** option. Returns 0 if validation fails, 1 if it succeeds. Sets or clears the **invalid** state accordingly. See [Validation] below for more details.


# Validation

The **-validate**, **-validatecommand**, and **-invalidcommand** options are used to enable entry widget validation.

## Validation modes

There are two main validation modes: *prevalidation*, in which the **-validatecommand** is evaluated prior to each edit and the return value is used to determine whether to accept or reject the change; and *revalidation*, in which the **-validatecommand** is evaluated to determine whether the current value is valid.

The **-validate** option determines when validation occurs; it may be set to any of the following values:

**none**
: Default.  This means validation will only occur when specifically requested by the **validate** widget command.

**key**
: The entry will be prevalidated prior to each edit (specifically, whenever the **insert** or **delete** widget commands are called). If prevalidation fails, the edit is rejected.

**focus**
: The entry is revalidated when the entry receives or loses focus.

**focusin**
: The entry is revalidated when the entry receives focus.

**focusout**
: The entry is revalidated when the entry loses focus.

**all**
: Validation is performed for all above conditions.


The **-invalidcommand** is evaluated whenever the **-validatecommand** returns a false value.

The **-validatecommand** and **-invalidcommand** may modify the entry widget's value via the widget **insert** or **delete** commands, or by setting the linked **-textvariable**. If either does so during prevalidation, then the edit is rejected regardless of the value returned by the **-validatecommand**.

If **-validatecommand** is empty (the default), validation always succeeds.

## Validation script substitutions

It is possible to perform percent substitutions on the **-validatecommand** and **-invalidcommand**, just as in a [bind] script. The following substitutions are recognized:

**%d**
: Type of action: 1 for **insert** prevalidation, 0 for **delete** prevalidation, or -1 for revalidation.

**%i**
: Index of character string to be inserted/deleted, if any, otherwise -1.

**%P**
: In prevalidation, the new value of the entry if the edit is accepted. In revalidation, the current value of the entry.

**%s**
: The current value of entry prior to editing.

**%S**
: The text string being inserted/deleted, if any, {} otherwise.

**%v**
: The current value of the **-validate** option.

**%V**
: The validation condition that triggered the callback (**key**, **focusin**, **focusout**, or **forced**).

**%W**
: The name of the entry widget.


## Differences from tk entry widget validation

The standard Tk entry widget automatically disables validation (by setting **-validate** to **none**) if the **-validatecommand** or **-invalidcommand** modifies the entry's value. The Tk themed entry widget only disables validation if one of the validation scripts raises an error, or if **-validatecommand** does not return a valid boolean value. (Thus, it is not necessary to re-enable validation after modifying the entry value in a validation script).

In addition, the standard entry widget invokes validation whenever the linked **-textvariable** is modified; the Tk themed entry widget does not.

# Default bindings

The entry widget's default bindings enable the following behavior. In the descriptions below, "word" refers to a contiguous group of letters, digits, or "\_" characters, or any single character other than these.

 \\(bu
: Clicking mouse button 1 positions the insert cursor just before the character underneath the mouse cursor, sets the input focus to this widget, and clears any selection in the widget. Dragging with mouse button 1 down strokes out a selection between the insert cursor and the character under the mouse.

 \\(bu
: Double-clicking with mouse button 1 selects the word under the mouse and positions the insert cursor at the end of the word. Dragging after a double click strokes out a selection consisting of whole words.

 \\(bu
: Triple-clicking with mouse button 1 selects all of the text in the entry and positions the insert cursor at the end of the line.

 \\(bu
: The ends of the selection can be adjusted by dragging with mouse button 1 while the Shift key is down. If the button is double-clicked before dragging then the selection will be adjusted in units of whole words.

 \\(bu
: Clicking mouse button 1 with the Control key down will position the insert cursor in the entry without affecting the selection.

 \\(bu
: If any normal printing characters are typed in an entry, they are inserted at the point of the insert cursor.

 \\(bu
: The view in the entry can be adjusted by dragging with the middle mouse button (button 2, or button 3 in TkAqua). If the middle mouse button is clicked without moving the mouse, the selection is copied into the entry at the position of the mouse cursor.

 \\(bu
: If the mouse is dragged out of the entry on the left or right sides while button 1 is pressed, the entry will automatically scroll to make more text visible (if there is more text off-screen on the side where the mouse left the window).

 \\(bu
: The Left and Right keys move the insert cursor one character to the left or right;  they also clear any selection in the entry. If Left or Right is typed with the Shift key down, then the insertion cursor moves and the selection is extended to include the new character. Control-Left and Control-Right move the insert cursor by words, and Control-Shift-Left and Control-Shift-Right move the insert cursor by words and also extend the selection. Control-b and Control-f behave the same as Left and Right, respectively.

 \\(bu
: The Home key and Control-a move the insert cursor to the beginning of the entry and clear any selection in the entry. Shift-Home moves the insert cursor to the beginning of the entry and extends the selection to that point.

 \\(bu
: The End key and Control-e move the insert cursor to the end of the entry and clear any selection in the entry. Shift-End moves the cursor to the end and extends the selection to that point.

 \\(bu
: Control-/ selects all the text in the entry.

 \\(bu
: Control-\\ clears any selection in the entry.

 \\(bu
: The standard Tk <<Cut>>, <<Copy>>, <<Paste>>, and <<Clear>> virtual events operate on the selection in the expected manner.

 \\(bu
: The Delete key deletes the selection, if there is one in the entry. If there is no selection, it deletes the character to the right of the insert cursor.

 \\(bu
: The BackSpace key and Control-h delete the selection, if there is one in the entry. If there is no selection, it deletes the character to the left of the insert cursor.

 \\(bu
: Control-d deletes the character to the right of the insert cursor.

 \\(bu
: Control-k deletes all the characters to the right of the insertion cursor.


# Widget states

In the **disabled** state, the entry cannot be edited and the text cannot be selected. In the **readonly** state, no insert cursor is displayed and the entry cannot be edited (specifically: the **insert** and **delete** commands have no effect). The **disabled** state is the same as **readonly**, and in addition text cannot be selected.

Note that changes to the linked **-textvariable** will still be reflected in the entry, even if it is disabled or readonly.

Typically, the text is "grayed-out" in the **disabled** state, and a different background is used in the **readonly** state.

The entry widget sets the **invalid** state if revalidation fails, and clears it whenever validation succeeds.

# Styling options

The class name for a **ttk::entry** is **TEntry**.

Dynamic states: **disabled**, **focus**, **readonly**.

**TEntry** styling options configurable with [ttk::style][ttk_style] are:

**-background** *color* For backwards compatibility, when using the aqua theme (for macOS), this option behaves as an alias for the **-fieldbackground** provided that no value is specified for **-fieldbackground**. Otherwise it is ignored. **-bordercolor** *color* .br **-darkcolor** *color* .br **-fieldbackground** *color* Some themes use a graphical background and their field background colors cannot be changed. **-foreground** *color* .br **-insertcolor** *color* .br **-insertwidth** *amount* .br **-lightcolor** *color* .br **-padding** *padding* .br **-placeholderforeground** *color* .br **-relief** *relief* .br **-selectbackground** *color* If empty, the selected text has no distinct background. **-selectborderwidth** *amount* .br **-selectforeground** *color* If empty, the **-foreground** color is used.

See the [ttk::style][ttk_style] manual page for information on how to configure ttk styles.


[bind]: bind.md
[entry]: entry.md
[selection]: selection.md
[string]: ../Tcl/string.md
[ttk_style]: ttk_style.md
[ttk_widget]: ttk_widget.md

