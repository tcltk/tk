---
CommandName: tk_popup
title: tk_popup
ManualSection: n
Version: 4.0
TclPart: Tk
TclDescription: Tk Built-In Commands
Links:
 - bind(n)
 - menu(n)
 - tk_optionMenu(n)
Keywords:
 - menu
 - popup
Copyright:
 - Copyright (c) 1994-1996 Sun Microsystems, Inc.
---

# Name

tk\_popup - Post a popup menu

# Synopsis

::: {.synopsis} :::
[tk\_popup]{.cmd} [menu]{.arg} [x]{.arg} [y]{.arg} [entry]{.optarg}
:::

# Description

This procedure posts a menu at a given position on the screen and configures Tk so that the menu and its cascaded children can be traversed with the mouse or the keyboard. *Menu* is the name of a menu widget and *x* and *y* are the root coordinates at which to display the menu. If *entry* is omitted or an empty string, the menu's upper left corner is positioned at the given point. Otherwise *entry* gives the index of an entry in *menu* and the menu will be positioned so that the entry is positioned over the given point.

On X11 the menu is a window of the application: **tk\_popup** returns as soon as the menu is posted, and the menu generates **<Map>** and **<Unmap>** events when it is posted and unposted.  On Windows and macOS the menu is displayed by the system, which runs its own event loop while the menu is posted: **tk\_popup** does not return before the menu is unposted again, and no **<Map>** or **<Unmap>** event is generated for it.

# Example

How to attach a simple popup menu to a widget.

```
# Create a menu
set m [menu .popupMenu]
$m add command -label "Example 1" -command bell
$m add command -label "Example 2" -command bell

# Create something to attach it to
pack [label .l -text "Click me!"]

# Arrange for the menu to pop up when the label is clicked
bind .l <Button-1> {tk_popup .popupMenu %X %Y}
```

