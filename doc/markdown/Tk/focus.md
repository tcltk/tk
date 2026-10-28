---
CommandName: focus
title: focus
ManualSection: n
Version: 4.0
TclPart: Tk
TclDescription: Tk Built-In Commands
Keywords:
 - events
 - focus
 - keyboard
 - top-level
 - window manager
Copyright:
 - Copyright (c) 1990-1994 The Regents of the University of California.
 - Copyright (c) 1994-1996 Sun Microsystems, Inc.
---

# Name

focus - Manage the input focus

# Synopsis

::: {.synopsis} :::
[focus]{.cmd}
[focus]{.cmd} [window]{.arg}
[focus]{.cmd} [option]{.arg} [arg]{.optdot}
:::

# Description

The **focus** command is used to manage the Tk input focus. At any given time, one window on each display is designated as the *focus window*;  any key press or key release events for the display are sent to that window. It is normally up to the window manager to redirect the focus among the top-level windows of a display.  For example, some window managers automatically set the input focus to a top-level window whenever the mouse enters it;  others redirect the input focus only when the user clicks on a window. Usually the window manager will set the focus only to top-level windows, leaving it up to the application to redirect the focus among the children of the top-level.

Tk remembers one focus window for each top-level (the most recent descendant of that top-level to receive the focus);  when the window manager gives the focus to a top-level, Tk automatically redirects it to the remembered window.  Within a top-level Tk uses an *explicit* focus model by default.  Moving the mouse within a top-level does not normally change the focus;  the focus changes only when a widget decides explicitly to claim the focus (e.g., because of a button click), or when the user types a key such as Tab that moves the focus.

The Tcl procedure [tk\_focusFollowsMouse][focusNext] may be invoked to create an *implicit* focus model:  it reconfigures Tk so that the focus is set to a window whenever the mouse enters it. The Tcl procedures [tk\_focusNext][focusNext] and [tk\_focusPrev][focusNext] implement a focus order among the windows of a top-level;  they are used in the default bindings for Tab and Shift-Tab, among other things.

The **focus** command can take any of the following forms:

**focus**
: Returns the path name of the focus window on the display containing the application's main window,  or an empty string if no window in this application has the focus on that display.   Note:  it is better to specify the display explicitly using **-displayof** (see below) so that the code will work in applications using multiple displays.

**focus** *window*
: If the application currently has the input focus on *window*'s display, this command resets the input focus for *window*'s display to *window* and returns an empty string. If the application does not currently have the input focus on *window*'s display, *window* will be remembered as the focus for its top-level;  the next time the focus arrives at the top-level, Tk will redirect it to *window*. If *window* is an empty string then the command does nothing.

**focus -displayof** *window*
: Returns the name of the focus window on the display containing *window*. If the focus window for *window*'s display is not in this application, the return value is an empty string.

**focus -force** *window*
: Sets the focus of *window*'s display to *window*, even if the application does not currently have the input focus for the display. This command should be used sparingly, if at all. In normal usage, an application should not claim the focus for itself;  instead, it should wait for the window manager to give it the focus. If *window* is an empty string then the command does nothing.

**focus -lastfor** *window*
: Returns the name of the most recent window to have the input focus among all the windows in the same top-level as *window*. If no window in that top-level has ever had the input focus, or if the most recent focus window has been deleted, then the name of the top-level is returned.  The return value is the window that will receive the input focus the next time the window manager gives the focus to the top-level.


# Quirks

When an internal window receives the input focus, Tk does not actually set the X focus to that window;  as far as X is concerned, the focus will stay on the top-level window containing the window with the focus. However, Tk generates FocusIn and FocusOut events just as if the X focus were on the internal window.   This approach gets around a number of problems that would occur if the X focus were actually moved; the fact that the X focus is on the top-level is invisible unless you use C code to query the X server directly.

# Example

To make a window that only participates in the focus traversal ring when a variable is set, add the following bindings to the widgets *before* and *after* it in that focus ring:

```
button .before -text "Before"
button .middle -text "Middle"
button .after  -text "After"
checkbutton .flag -variable traverseToMiddle -takefocus 0
pack .flag -side left
pack .before .middle .after
bind .before <Tab> {
   if {!$traverseToMiddle} {
      focus .after
      break
   }
}
bind .after <Shift-Tab> {
   if {!$traverseToMiddle} {
      focus .before
      break
   }
}
focus .before
```


[focusNext]: focusNext.md

