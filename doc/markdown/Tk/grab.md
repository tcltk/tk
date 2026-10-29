---
CommandName: grab
title: grab
ManualSection: n
Version: unknown
TclPart: Tk
TclDescription: Tk Built-In Commands
Links:
 - busy(n)
Keywords:
 - grab
 - keyboard events
 - pointer events
 - window
Copyright:
 - Copyright (c) 1992 The Regents of the University of California.
 - Copyright (c) 1994-1996 Sun Microsystems, Inc.
---

# Name

grab - Confine pointer and keyboard events to a window sub-tree

# Synopsis

::: {.synopsis} :::
[grab]{.cmd} [-global]{.optlit} [window]{.arg}
[grab]{.cmd} [option]{.arg} [arg]{.optdot}
:::

# Description

This command implements simple pointer and keyboard grabs for Tk. Tk's grabs are different than the grabs described in the Xlib documentation. When a grab is set for a particular window, Tk restricts all pointer events to the grab window and its descendants in Tk's window hierarchy. Whenever the pointer is within the grab window's subtree, the pointer will behave exactly the same as if there had been no grab at all and all events will be reported in the normal fashion. When the pointer is outside *window*'s tree, button presses and releases and mouse motion events are reported to *window*, and window entry and window exit events are ignored. The grab subtree "owns" the pointer: windows outside the grab subtree will be visible on the screen but they will be insensitive until the grab is released. The tree of windows underneath the grab window can include top-level windows, in which case all of those top-level windows and their descendants will continue to receive mouse events during the grab.

Two forms of grabs are possible:  local and global. A local grab affects only the grabbing application:  events will be reported to other applications as if the grab had never occurred. Grabs are local by default. A global grab locks out all applications on the screen, so that only the given subtree of the grabbing application will be sensitive to pointer events (mouse button presses, mouse button releases, pointer motions, window entries, and window exits). During global grabs the window manager will not receive pointer events either.

During local grabs, keyboard events (key presses and key releases) are delivered as usual:  the window manager controls which application receives keyboard events, and if they are sent to any window in the grabbing application then they are redirected to the focus window. During a global grab Tk grabs the keyboard so that all keyboard events are always sent to the grabbing application. The [focus] command is still used to determine which window in the application receives the keyboard events. The keyboard grab is released when the grab is released.

On macOS a global grab affects all windows created by one Tk process. No window in that process other than the grab window can even be focused, hence no other window receives key or mouse events.  A local grab on macOS affects all windows created by one Tcl interpreter.  It is possible to focus any window belonging to the Tk process during a local grab but the grab window is the only window created by its interpreter which receives key or mouse events.  Windows belonging to the same process but created by different interpreters continue to receive key and mouse events normally.

Grabs apply to particular displays.  If an application has windows on multiple displays then it can establish a separate grab on each display. The grab on a particular display affects only the windows on that display. It is possible for different applications on a single display to have simultaneous local grabs, but only one application can have a global grab on a given display at once.

The **grab** command can take any of the following forms:

**grab** ?**-global**? *window*
: Same as **grab set**, described below.

[grab]{.cmd} [current]{.sub} [window]{.optarg}
: If *window* is specified, returns the name of the current grab window in this application for *window*'s display, or an empty string if there is no such window. If *window* is omitted, the command returns a list whose elements are all of the windows grabbed by this application for all displays, or an empty string if the application has no grabs.

[grab]{.cmd} [release]{.sub} [window]{.arg}
: Releases the grab on *window* if there is one, otherwise does nothing.  Returns an empty string.

[grab]{.cmd} [set]{.sub} [-global]{.optlit} [window]{.arg}
: Sets a grab on *window*.  If **-global** is specified then the grab is global, otherwise it is local. If a grab was already in effect for this application on *window*'s display then it is automatically released. If there is already a grab on *window* and it has the same global/local form as the requested grab, then the command does nothing.  Returns an empty string.

[grab]{.cmd} [status]{.sub} [window]{.arg}
: Returns **none** if no grab is currently set on *window*, **local** if a local grab is set on *window*, and **global** if a global grab is set.


# Warning

It is very easy to use global grabs to render a display completely unusable (e.g. by setting a grab on a widget which does not respond to events and not providing any mechanism for releasing the grab).  Take *extreme* care when using them!

# Bugs

It took an incredibly complex and gross implementation to produce the simple grab effect described above. Given the current implementation, it is not safe for applications to use the Xlib grab facilities at all except through the Tk grab procedures. If applications try to manipulate X's grab mechanisms directly, things will probably break.

If a single process is managing several different Tk applications, only one of those applications can have a local grab for a given display at any given time.  If the applications are in different processes, this restriction does not exist.

# Example

Set a grab so that only one button may be clicked out of a group.  The other buttons are unresponsive to the mouse until the middle button is clicked.

```
pack [button .b1 -text "Click me! #1" -command {destroy .b1}]
pack [button .b2 -text "Click me! #2" -command {destroy .b2}]
pack [button .b3 -text "Click me! #3" -command {destroy .b3}]
grab .b2
```


[focus]: focus.md

