---
CommandName: tk_dialog
title: tk_dialog
ManualSection: n
Version: 4.1
TclPart: Tk
TclDescription: Tk Built-In Commands
Links:
 - tk_messageBox(n)
Keywords:
 - bitmap
 - dialog
 - modal
Copyright:
 - Copyright (c) 1992 The Regents of the University of California.
 - Copyright (c) 1994-1996 Sun Microsystems, Inc.
---

# Name

tk\_dialog - Create modal dialog and wait for response

# Synopsis

::: {.synopsis} :::
[tk\_dialog]{.cmd} [window]{.arg} [title]{.arg} [text]{.arg} [bitmap]{.arg} [default]{.arg} [string]{.arg} [string]{.arg} [...]{.arg}
:::

# Description

This procedure is part of the Tk script library. It is largely *deprecated* by the [tk\_messageBox][messageBox]. Its arguments describe a dialog box:

*window*
: Name of top-level window to use for dialog.  Any existing window by this name is destroyed.

*title*
: Text to appear in the window manager's title bar for the dialog.

*text*
: Message to appear in the top portion of the dialog box.

*bitmap*
: If non-empty, specifies a bitmap (in a form suitable for [Tk\_GetBitmap][GetBitmap]) to display in the top portion of the dialog, to the left of the text. If this is an empty string then no bitmap is displayed in the dialog.

*default*
: If this is an integer greater than or equal to zero, then it gives the index of the button that is to be the default button for the dialog (0 for the leftmost button, and so on). If negative or an empty string then there will not be any default button.

*string*
: There will be one button for each of these arguments. Each *string* specifies text to display in a button, in order from left to right.


After creating a dialog box, **tk\_dialog** waits for the user to select one of the buttons either by clicking on the button with the mouse or by typing return to invoke the default button (if any). Then it returns the index of the selected button:  0 for the leftmost button, 1 for the button next to it, and so on. If the dialog's window is destroyed before the user selects one of the buttons, then -1 is returned.

While waiting for the user to respond, **tk\_dialog** sets a local grab.  This prevents the user from interacting with the application in any way except to invoke the dialog box.

# Example

```
set reply [tk_dialog .foo "The Title" "Do you want to say yes?" \
        questhead 0 Yes No "I'm not sure"]
```


[GetBitmap]: ../TkCAPI/GetBitmap.md
[messageBox]: messageBox.md

