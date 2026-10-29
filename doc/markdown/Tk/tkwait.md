---
CommandName: tkwait
title: tkwait
ManualSection: n
Version: unknown
TclPart: Tk
TclDescription: Tk Built-In Commands
Links:
 - bind(n)
 - vwait(n)
Keywords:
 - variable
 - visibility
 - wait
 - window
Copyright:
 - Copyright (c) 1992 The Regents of the University of California.
 - Copyright (c) 1994-1996 Sun Microsystems, Inc.
---

# Name

tkwait - Wait for variable to change or window to be destroyed

# Synopsis

::: {.synopsis} :::
[tkwait]{.cmd} [variable]{.sub} [name]{.arg}
[tkwait]{.cmd} [visibility]{.sub} [name]{.arg}
[tkwait]{.cmd} [window]{.sub} [name]{.arg}
:::

# Description

The **tkwait** command waits for one of several things to happen, then it returns without taking any other actions. The return value is always an empty string. If the first argument is **variable** (or any abbreviation of it) then the second argument is the name of a global variable and the command waits for that variable to be modified. If the first argument is **visibility** (or any abbreviation of it) then the second argument is the name of a window and the **tkwait** command waits for a change in its visibility state (as indicated by the arrival of a VisibilityNotify event).  This form is typically used to wait for a newly-created window to appear on the screen before taking some action. If the first argument is **window** (or any abbreviation of it) then the second argument is the name of a window and the **tkwait** command waits for that window to be destroyed. This form is typically used to wait for a user to finish interacting with a dialog box before using the result of that interaction.

While the **tkwait** command is waiting it processes events in the normal fashion, so the application will continue to respond to user interactions. If an event handler invokes **tkwait** again, the nested call to **tkwait** must complete before the outer call can complete.

