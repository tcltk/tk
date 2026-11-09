---
CommandName: tk fileicon
title: tk fileicon
ManualSection: n
Version: unknown
TclPart: Tk
TclDescription: Tk Built-In Commands
Keywords:
 - image
 - icon
Copyright:
 - Copyright (c) 2025 Kevin Walzer.
---

# Name

tk fileicon - Platform-native icon image for file type/extension.

# Synopsis

::: {.synopsis} :::
[tk]{.cmd} [fileicon]{.sub} [file]{.arg} [size]{.arg}
:::

# Description

The [tk fileicon][tk] command creates a platform-specific file icon. Its intent is to provide a visual representation of a file type that is expected by users on that platform. The command takes two arguments -  an aboslute file path and an icon size, which be any size (on Windows these map to the closest of 16, 32, 48 or 256 pixels because of API constraints).

# Example

Here is an example of the [tk fileicon][tk] code:

```
set img [tk fileicon [file normalize text.txt] 32]
pack [label .l -text "text.txt" -image $img -compound top]
```

# Platform notes

On Windows and macOS, system API's are used; on X11, icons from the Tango project - the default icon theme for the Gnome desktop - are used.


[tk]: tk.md

