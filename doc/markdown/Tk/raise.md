---
CommandName: raise
title: raise
ManualSection: n
Version: 3.3
TclPart: Tk
TclDescription: Tk Built-In Commands
Links:
 - lower(n)
Keywords:
 - obscure
 - raise
 - stacking order
Copyright:
 - Copyright (c) 1990 The Regents of the University of California.
 - Copyright (c) 1994-1996 Sun Microsystems, Inc.
---

# Name

raise - Change a window's position in the stacking order

# Synopsis

::: {.synopsis} :::
[raise]{.cmd} [window]{.arg} [aboveThis]{.optarg}
:::

# Description

If the *aboveThis* argument is omitted then the command raises *window* so that it is above all of its siblings in the stacking order (it will not be obscured by any siblings and will obscure any siblings that overlap it). If *aboveThis* is specified then it must be the path name of a window that is either a sibling of *window* or the descendant of a sibling of *window*. In this case the **raise** command will insert *window* into the stacking order just above *aboveThis* (or the ancestor of *aboveThis* that is a sibling of *window*); this could end up either raising or lowering *window*.

All [toplevel] windows may be restacked with respect to each other, whatever their relative path names, but the window manager is not obligated to strictly honor requests to restack.

On macOS raising an iconified [toplevel] window causes it to be deiconified.

# Example

Make a button appear to be in a sibling frame that was created after it. This is is often necessary when building GUIs in the style where you create your activity widgets first before laying them out on the display:

```
button .b -text "Hi there!"
pack [frame .f -background blue]
pack [label .f.l1 -text "This is above"]
pack .b -in .f
pack [label .f.l2 -text "This is below"]
raise .b
```


[toplevel]: toplevel.md

