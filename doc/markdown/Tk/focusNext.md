---
CommandName: tk_focusNext
title: tk_focusNext
ManualSection: n
Version: 4.0
TclPart: Tk
TclDescription: Tk Built-In Commands
Keywords:
 - focus
 - keyboard traversal
 - top-level
Copyright:
 - Copyright (c) 1994 The Regents of the University of California.
 - Copyright (c) 1994-1996 Sun Microsystems, Inc.
---

# Name

tk\_focusNext, tk\_focusPrev, tk\_focusFollowsMouse - Utility procedures for managing the input focus.

# Synopsis

::: {.synopsis} :::
[tk\_focusNext]{.cmd} [window]{.arg}
[tk\_focusPrev]{.cmd} [window]{.arg}
[tk\_focusFollowsMouse]{.cmd}
:::

# Description

**tk\_focusNext** is a utility procedure used for keyboard traversal. It returns the "next" window after *window* in focus order. The focus order is determined by the stacking order of windows and the structure of the window hierarchy. Among siblings, the focus order is the same as the stacking order, with the lowest window being first. If a window has children, the window is visited first, followed by its children (recursively), followed by its next sibling. Top-level windows other than *window* are skipped, so that **tk\_focusNext** never returns a window in a different top-level from *window*.

After computing the next window, **tk\_focusNext** examines the window's **-takefocus** option to see whether it should be skipped. If so, **tk\_focusNext** continues on to the next window in the focus order, until it eventually finds a window that will accept the focus or returns back to *window*.

**tk\_focusPrev** is similar to **tk\_focusNext** except that it returns the window just before *window* in the focus order.

**tk\_focusFollowsMouse** changes the focus model for the application to an implicit one where the window under the mouse gets the focus. After this procedure is called, whenever the mouse enters a window Tk will automatically give it the input focus. The [focus] command may be used to move the focus to a window other than the one under the mouse, but as soon as the mouse moves into a new window the focus will jump to that window.

Note that at present there is no built-in support for returning the application to an explicit focus model;  to do this you will have to write a script that deletes the bindings created by **tk\_focusFollowsMouse**.


[focus]: focus.md

