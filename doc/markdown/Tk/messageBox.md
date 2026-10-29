---
CommandName: tk_messageBox
title: tk_messageBox
ManualSection: n
Version: 4.2
TclPart: Tk
TclDescription: Tk Built-In Commands
Keywords:
 - message box
Copyright:
 - Copyright (c) 1996 Sun Microsystems, Inc.
---

# Name

tk\_messageBox - pops up a message window and waits for user response.

# Synopsis

::: {.synopsis} :::
[tk\_messageBox]{.cmd} [option value]{.optdot}
:::

# Description

This procedure creates and displays a message window with an application-specified message, an icon and a set of buttons.  Each of the buttons in the message window is identified by a unique symbolic name (see the **-type** options).  After the message window is popped up, **tk\_messageBox** waits for the user to select one of the buttons. Then it returns the symbolic name of the selected button.

The following option-value pairs are supported:

[-command]{.lit} [string]{.arg}
: Specifies the prefix of a Tcl command to invoke when the user closes the dialog. The actual command consists of *string* followed by a space and the name of the button clicked by the user to close the dialog. This is only available on macOS.

[-default]{.lit} [name]{.arg}
: *Name* gives the symbolic name of the default button for this message window ( "ok", "cancel", and so on). See **-type** for a list of the symbolic names.  If this option is not specified, the first button in the dialog will be made the default.

[-detail]{.lit} [string]{.arg}
: Specifies an auxiliary message to the main message given by the **-message** option. The message detail will be presented beneath the main message and, where supported by the OS, in a less emphasized font than the main message.

[-icon]{.lit} [iconImage]{.arg}
: Specifies an icon to display. *IconImage* must be one of the following: **error**, **info**, **question** or **warning**. If this option is not specified, then the info icon will be displayed.

[-message]{.lit} [string]{.arg}
: Specifies the message to display in this message box. The default value is an empty string.

[-parent]{.lit} [window]{.arg}
: Makes *window* the logical parent of the message box. The message box is displayed on top of its parent window.

[-title]{.lit} [titleString]{.arg}
: Specifies a string to display as the title of the message box. The default value is an empty string.

[-type]{.lit} [predefinedType]{.arg}
: Arranges for a predefined set of buttons to be displayed. The following values are possible for *predefinedType*:

    **abortretryignore**
    : Displays three buttons whose symbolic names are **abort**, **retry** and **ignore**.

    **ok**
    : Displays one button whose symbolic name is **ok**. This is the default.

    **okcancel**
    : Displays two buttons whose symbolic names are **ok** and **cancel**.

    **retrycancel**
    : Displays two buttons whose symbolic names are **retry** and **cancel**.

    **yesno**
    : Displays two buttons whose symbolic names are **yes** and **no**.

    **yesnocancel**
    : Displays three buttons whose symbolic names are **yes**, **no** and **cancel**.



# Example

```
set answer [tk_messageBox -message "Really quit?" \
        -icon question -type yesno \
        -detail "Select \"Yes\" to make the application exit"]
switch -- $answer {
    yes exit
    no {tk_messageBox -message "I know you like this application!" \
            -type ok}
}
```

