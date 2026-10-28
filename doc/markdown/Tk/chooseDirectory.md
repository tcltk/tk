---
CommandName: tk_chooseDirectory
title: tk_chooseDirectory
ManualSection: n
Version: 8.3
TclPart: Tk
TclDescription: Tk Built-In Commands
Links:
 - tk_getOpenFile(n)
 - tk_getSaveFile(n)
Keywords:
 - directory
 - selection
 - dialog
 - platform-specific
Copyright:
 - Copyright (c) 1998-2000 Scriptics Corporation.
---

# Name

tk\_chooseDirectory - pops up a dialog box for the user to select a directory.

# Synopsis

::: {.synopsis} :::
[tk\_chooseDirectory]{.cmd} [option value]{.optdot}
:::

# Description

The procedure **tk\_chooseDirectory** pops up a dialog box for the user to select a directory. The following *option-value* pairs are possible as command line arguments:

[-command]{.lit} [string]{.arg}
: Specifies the prefix of a Tcl command to invoke when the user closes the dialog after having selected an item. This callback is not called if the user cancelled the dialog. The actual command consists of *string* followed by a space and the value selected by the user in the dialog. This is only available on macOS.

[-initialdir]{.lit} [dirname]{.arg}
: Specifies that the directories in *directory* should be displayed when the dialog pops up. If this parameter is not specified, the initial directory defaults to the current working directory on non-Windows systems and on Windows systems prior to Vista. On Vista and later systems, the initial directory defaults to the last user-selected directory for the application. If the parameter specifies a relative path, the return value will convert the relative path to an absolute path.

[-message]{.lit} [string]{.arg}
: Specifies a message to include in the client area of the dialog. This is only available on macOS.

[-mustexist]{.lit} [boolean]{.arg}
: Specifies whether the user may specify non-existent directories.  If this parameter is true, then the user may only select directories that already exist.  The default value is *false*.

[-parent]{.lit} [window]{.arg}
: Makes *window* the logical parent of the dialog. The dialog is displayed on top of its parent window. On macOS, this turns the file dialog into a sheet attached to the parent window.

[-title]{.lit} [titleString]{.arg}
: Specifies a string to display as the title of the dialog box. If this option is not specified, then a default title will be displayed.


# Example

```
set dir [tk_chooseDirectory \
        -initialdir ~ -title "Choose a directory"]
if {$dir eq ""} {
   label .l -text "No directory selected"
} else {
   label .l -text "Selected $dir"
}
```

