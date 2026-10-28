---
CommandName: fontchooser
title: fontchooser
ManualSection: n
Version: unknown
TclPart: Tk
TclDescription: Tk Built-In Commands
Links:
 - font(n)
 - tk(n)
Keywords:
 - dialog
 - font
 - font selection
 - font chooser
 - font panel
Copyright:
 - Copyright (c) 2008 Daniel A. Steffen <das@users.sourceforge.net>
---

# Name

fontchooser - control font selection dialog

# Synopsis

::: {.synopsis} :::
[tk]{.cmd} [fontchooser]{.sub} [configure]{.lit} [-option value]{.optdot}
[tk]{.cmd} [fontchooser]{.sub} [show]{.lit}
[tk]{.cmd} [fontchooser]{.sub} [hide]{.lit}
:::

# Description

The [tk fontchooser][tk] command controls the Tk font selection dialog. It uses the native platform font selection dialog where available, or a dialog implemented in Tcl otherwise.

Unlike most of the other Tk dialog commands, [tk fontchooser][tk] does not return an immediate result, as on some platforms (macOS) the standard font dialog is modeless while on others (Windows) it is modal. To accommodate this difference, all user interaction with the dialog will be communicated to the caller via callbacks or virtual events.

The [tk fontchooser][tk] command can have one of the following forms:

[tk]{.cmd} [fontchooser]{.sub} [configure]{.lit} [-option value]{.optdot}
: Set or query one or more of the configurations options below (analogous to Tk widget configuration).

[tk]{.cmd} [fontchooser]{.sub} [show]{.lit}
: Show the font selection dialog. Depending on the platform, may return immediately or only once the dialog has been withdrawn.

[tk]{.cmd} [fontchooser]{.sub} [hide]{.lit}
: Hide the font selection dialog if it is visible and cause any pending [tk fontchooser][tk] **show** command to return.


# Configuration options

[-parent]{.lit}
: Specifies/returns the logical parent window of the font selection dialog (similar to the **-parent** option to other dialogs). The font selection dialog is hidden if it is visible when the parent window is destroyed.

[-title]{.lit}
: Specifies/returns the title of the dialog. Has no effect on platforms where the font selection dialog does not support titles.

[-font]{.lit}
: Specifies/returns the font that is currently selected in the dialog if it is visible, or that will be initially selected when the dialog is shown (if supported by the platform). Can be set to the empty string to indicate that no font should be selected. Fonts can be specified in any form given by the "FONT DESCRIPTION" section in the [font] manual page.

[-command]{.lit}
: Specifies/returns the command prefix to be called when a font selection has been made by the user. The command prefix is evaluated at the global level after having the specification of the selected font appended. On platforms where the font selection dialog offers the user control of further font attributes (such as color), additional key/value pairs may be appended before evaluation. Can be set to the empty string to indicate that no callback should be invoked. Fonts are specified by a list of form \[3\] of the "FONT DESCRIPTION" section in the [font] manual page (i.e. a list of the form *{family size style ?style ...?}*).

[-visible]{.lit}
: Read-only option that returns a boolean indicating whether the font selection dialog is currently visible. Attempting to set this option results in an error. 


# Virtual events

**<<TkFontchooserVisibility>>**
: Sent to the dialog parent whenever the visibility of the font selection dialog changes, both as a result of user action (e.g. disposing of the dialog via OK/Cancel button or close box) and of the [tk fontchooser][tk] **show**/**hide** commands being called. Binding scripts can determine the current visibility of the dialog by querying the **-visible** configuration option.

**<<TkFontchooserFontChanged>>**
: Sent to the dialog parent whenever the font selection dialog is visible and the selected font changes, both as a result of user action and of the **-font** configuration option being set. Binding scripts can determine the currently selected font by querying the **-font** configuration option.


# Notes

Callers should not expect a result from [tk fontchooser][tk] **show** and may not assume that the dialog has been withdrawn or closed when the command returns. All user interaction with the dialog is communicated to the caller via the **-command** callback and the **<<TkFontchooser\*>>** virtual events. It is implementation dependent which exact user actions result in the callback being called resp. the virtual events being sent. Where an Apply or OK button is present in the dialog, that button will trigger the **-command** callback and **<<TkFontchooserFontChanged>>** virtual event. On some implementations other user actions may also have that effect; on macOS for instance, the standard font selection dialog immediately reflects all user choices to the caller.

In the presence of multiple widgets intended to be influenced by the font selection dialog, care needs to be taken to correctly handle focus changes: the font selected in the dialog should always match the current font of the widget with the focus, and the **-command** callback should only act on the widget with the focus. The recommended practice is to set font dialog **-font** and **-command** configuration options in per-widget **<FocusIn>** handlers (and if necessary to unset them - i.e. set to the empty string - in corresponding **<FocusOut>** handlers). This is particularly important for implementers of library code using the font selection dialog, to avoid conflicting with application code that may also want to use the dialog.

Because the font selection dialog is application-global, in the presence of multiple interpreters calling [tk fontchooser][tk], only the **-command** callback set by the interpreter that most recently called [tk fontchooser][tk] **configure** or [tk fontchooser][tk] **show** will be invoked in response to user action and only the **-parent** set by that interpreter will receive **<<TkFontchooser\*>>** virtual events.

The font dialog implementation may only store (and return) [font] **actual** data as the value of the **-font** configuration option. This can be an issue when **-font** is set to a named font, if that font is subsequently changed, the font dialog **-font** option needs to be set again to ensure its selected font matches the new value of the named font.

# Example

```
proc fontchooserDemo {} {
    wm title . "Font Chooser Demo"
    tk fontchooser configure -parent .
    button .b -command fontchooserToggle -takefocus 0
    fontchooserVisibility .b
    bind . <<TkFontchooserVisibility>> \
            [list fontchooserVisibility .b]
    foreach w {.t1 .t2} {
        text $w -width 20 -height 4 -borderwidth 1 -relief solid
        bind $w <FocusIn> [list fontchooserFocus $w]
        $w insert end "Text Widget $w"
    }
    .t1 configure -font {Courier 14}
    .t2 configure -font {Times 16}
    pack .b .t1 .t2; focus .t1
}
proc fontchooserToggle {} {
    tk fontchooser [expr {
            [tk fontchooser configure -visible] ?
            "hide" : "show"}]
}
proc fontchooserVisibility {w} {
    $w configure -text [expr {
            [tk fontchooser configure -visible] ?
            "Hide Font Dialog" : "Show Font Dialog"}]
}
proc fontchooserFocus {w} {
    tk fontchooser configure -font [$w cget -font] \
            -command [list fontchooserFontSelection $w]
}
proc fontchooserFontSelection {w font args} {
    $w configure -font [font actual $font]
}
fontchooserDemo
```


[font]: font.md
[tk]: tk.md

