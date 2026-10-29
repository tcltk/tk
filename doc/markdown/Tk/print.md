---
CommandName: tk print
title: tk print
ManualSection: n
Version: unknown
TclPart: Tk
TclDescription: Tk Built-In Commands
Links:
 - canvas(n)
 - text(n)
 - tk(n)
Keywords:
 - print
 - output
 - graphics
 - text
 - canvas
Copyright:
 - Copyright (c) 2021 Kevin Walzer
---

# Name

print - Print canvas and text widgets using native dialogs and APIs.

# Synopsis

::: {.synopsis} :::
[tk]{.cmd} [print]{.sub} [window]{.arg}
:::

# Description

The [tk print][tk] command posts a dialog that allows users to print output from the [canvas] and [text] widgets. The printing will be done using platform-native APIs and dialogs where available.

The [canvas] widget has long supported PostScript export and both PostScript and text files can be sent directly to a printer on Unix-like systems using the "lp" and "lpr" Unix commands, and the [tk print][tk] command does not supersede that functionality; it builds on it. The [tk print][tk] command is a fuller implementation that uses native dialogs on macOS and Windows, and a Tk-based dialog that provides parallel functionality on X11.

# Platform notes

**macOS**
: The Mac implementation uses native print dialogs and relies on the underlying Common Unix Printing System (CUPS) to render text output from the text widget and PDF conversion of the canvas data to the printer or to a PDF file.

**Windows**
: The Windows implementation is based on the GDI (Graphics Device Interface) API. Because there are slight differences in how GDI and Tk's [canvas] widget display graphics, printed output from the [canvas] on Windows may not be identical to screen rendering.

**X11**
: The X11 implementation uses a Tk GUI to configure print jobs for sending to a printer via the "lpr" or "lp" commands or via the Common Unix Printing System (CUPS) API if libcups is installed. While these commands have a large number of parameters for configuring print jobs, printers vary widely in how they support these parameters. As a result, only printer selection and number of copies are configured as arguments to the print command; many aspects of print rendering, such as grayscale or color for the canvas, are instead configured when PostScript is generated.



[canvas]: canvas.md
[text]: text.md
[tk]: tk.md

