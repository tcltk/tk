---
CommandName: tk_chooseColor
title: tk_chooseColor
ManualSection: n
Version: 4.2
TclPart: Tk
TclDescription: Tk Built-In Commands
Keywords:
 - color
 - color selection
 - dialog
Copyright:
 - Copyright (c) 1996 Sun Microsystems, Inc.
---

# Name

tk\_chooseColor - pops up a dialog box for the user to select a color.

# Synopsis

::: {.synopsis} :::
[tk\_chooseColor]{.cmd} [option value]{.optdot}
:::

# Description

The procedure **tk\_chooseColor** pops up a dialog box for the user to select a color. The following *option-value* pairs are possible as command line arguments:

[-initialcolor]{.lit} [color]{.arg}
: Specifies the color to display in the color dialog when it pops up. *color* must be in a form acceptable to the [Tk\_GetColor][GetColor] function.

[-parent]{.lit} [window]{.arg}
: Makes *window* the logical parent of the color dialog. The color dialog is displayed on top of its parent window.

[-title]{.lit} [titleString]{.arg}
: Specifies a string to display as the title of the dialog box. If this option is not specified, then a default title will be displayed.


If the user selects a color, **tk\_chooseColor** will return the name of the color in a form acceptable to [Tk\_GetColor][GetColor].  If the user cancels the operation, both commands will return the empty string.

# Example

```
button .b -bg [tk_chooseColor -initialcolor gray -title "Choose color"]
```


[GetColor]: ../TkCAPI/GetColor.md

