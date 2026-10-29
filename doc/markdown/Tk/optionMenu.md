---
CommandName: tk_optionMenu
title: tk_optionMenu
ManualSection: n
Version: 4.0
TclPart: Tk
TclDescription: Tk Built-In Commands
Keywords:
 - option menu
Copyright:
 - Copyright (c) 1990-1994 The Regents of the University of California.
 - Copyright (c) 1994-1996 Sun Microsystems, Inc.
---

# Name

tk\_optionMenu - Create an option menubutton and its menu

# Synopsis

::: {.synopsis} :::
[tk\_optionMenu]{.cmd} [pathName]{.arg} [varName]{.arg} [value]{.arg} [value value]{.optdot}
:::

# Description

This procedure creates an option menubutton whose name is *pathName*, plus an associated menu. Together they allow the user to select one of the values given by the *value* arguments. The current value will be stored in the global variable whose name is given by *varName* and it will also be displayed as the label in the option menubutton. The user can click on the menubutton to display a menu containing all of the *value*s and thereby select a new value. Once a new value is selected, it will be stored in the variable and appear in the option menubutton. The current value can also be changed by setting the variable.

The return value from **tk\_optionMenu** is the name of the menu associated with *pathName*, so that the caller can change its configuration options or manipulate it in other ways.

# Example

```
tk_optionMenu .foo myVar Foo Bar Boo Spong Wibble
pack .foo
```

