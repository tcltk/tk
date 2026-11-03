---
CommandName: tk accessible
title: tk accessible
ManualSection: n
Version: unknown
TclPart: Tk
TclDescription: Tk Built-In Commands
Keywords:
 - widget
 - tk
Copyright:
 - Copyright (c) 2025 Kevin Walzer
---

# Name

tk accessible - Adds screenreader support for core Tk and ttk  widgets.

# Synopsis

::: {.synopsis} :::
[tk]{.cmd} [accessible]{.sub} [add\_acc\_object]{.lit} [window]{.arg}
[tk]{.cmd} [accessible]{.sub} [check\_screenreader]{.lit}
[tk]{.cmd} [accessible]{.sub} [emit\_selection\_change]{.lit} [window]{.arg}
[tk]{.cmd} [accessible]{.sub} [emit\_focus\_change]{.lit} [window]{.arg}
[tk]{.cmd} [accessible]{.sub} [get\_acc\_action]{.lit} [window]{.arg}
[tk]{.cmd} [accessible]{.sub} [get\_acc\_description]{.lit} [window]{.arg}
[tk]{.cmd} [accessible]{.sub} [get\_acc\_help]{.lit} [window]{.arg}
[tk]{.cmd} [accessible]{.sub} [get\_acc\_name]{.lit} [window]{.arg}
[tk]{.cmd} [accessible]{.sub} [get\_acc\_role]{.lit} [window]{.arg}
[tk]{.cmd} [accessible]{.sub} [get\_acc\_state]{.lit} [window]{.arg}
[tk]{.cmd} [accessible]{.sub} [get\_acc\_value]{.lit} [window]{.arg}
[tk]{.cmd} [accessible]{.sub} [set\_acc\_action]{.lit} [window]{.arg} [value]{.arg}
[tk]{.cmd} [accessible]{.sub} [set\_acc\_description]{.lit} [window]{.arg} [value]{.arg}
[tk]{.cmd} [accessible]{.sub} [set\_acc\_help]{.lit} [window]{.arg} [value]{.arg}
[tk]{.cmd} [accessible]{.sub} [set\_acc\_name]{.lit} [window]{.arg} [value]{.arg}
[tk]{.cmd} [accessible]{.sub} [set\_acc\_role]{.lit} [window]{.arg} [value]{.arg}
[tk]{.cmd} [accessible]{.sub} [set\_acc\_state]{.lit} [window]{.arg} [value]{.arg}
[tk]{.cmd} [accessible]{.sub} [set\_acc\_value]{.lit} [window]{.arg} [value]{.arg}
:::

# Description

The [tk accessible][tk] command makes core Tk and ttk widgets accessible to a screen reader.

## Introduction

All major computing platforms provide one or more API's to make GUI applications accessible to visually-impaired users who work using screen reader tools. Screen readers provide auditory feedback to users of a GUI application, and may provide additional or alternative methods to navigating GUI's apart from standard keyboard navigation. The [tk accessible][tk] command layers a platform neutral API on top of platform-specific API's to support accessibility on Linux/Unix, macOS, and Windows.

The [tk accessible][tk] command is intended to provide a basic accessible-out-of-the-box experience for end users and requires little additional work by developers. If a screen reader is in operation, Tcl/Tk applications will be accessible. The [tk accessible][tk] command set does allow for additional customization on the part of developers. And widgets that are not part of the core Tk widget set can enable accessibility by using the tk accessible command set to specific the accessible role, name, description, value, state (if applicable), and action (if applicable).

## Operations

The following operations are available for the [tk accessible][tk] command:

[tk]{.cmd} [accessible]{.sub} [add\_acc\_object]{.lit} [window]{.arg}
: Registers the widget with the platform's accessibility API and enables accessibility for the widget.

[tk]{.cmd} [accessible]{.sub} [check\_screenreader]{.lit}
: Checks to see if the system is running a screen reader or other accessibility process.

[tk]{.cmd} [accessible]{.sub} [emit\_focus\_change]{.lit} [window]{.arg}
: Notifies the accessibility system when focus changes.

[tk]{.cmd} [accessible]{.sub} [emit\_selection\_change]{.lit} [window]{.arg}
: Notifies the system when selection, value or other data associated with the widget changes.

[tk]{.cmd} [accessible]{.sub} [get\_acc\_action]{.lit} [window]{.arg}
: Returns the widget's accessibility action.

[tk]{.cmd} [accessible]{.sub} [get\_acc\_description]{.lit} [window]{.arg}
: Returns the widget's accessibility description.

[tk]{.cmd} [accessible]{.sub} [get\_acc\_help]{.lit} [window]{.arg}
: Returns the widget's accessibility help.** 

[tk]{.cmd} [accessible]{.sub} [get\_acc\_name]{.lit} [window]{.arg}
: Returns the widget's accessibility name.

[tk]{.cmd} [accessible]{.sub} [get\_acc\_role]{.lit} [window]{.arg}
: Returns the widget's accessibility role.

[tk]{.cmd} [accessible]{.sub} [get\_acc\_state]{.lit} [window]{.arg}
: Returns the widget's accessibility state.

[tk]{.cmd} [accessible]{.sub} [get\_acc\_value]{.lit} [window]{.arg}
: Returns the widget's accessibility value.

[tk]{.cmd} [accessible]{.sub} [set\_acc\_action]{.lit} [window]{.arg} [value]{.arg}
: ** Sets the command associated with the widget, if any.

[tk]{.cmd} [accessible]{.sub} [set\_acc\_description]{.lit} [window]{.arg} [value]{.arg}
: Longer string providing additional detail about the widget. Depending on the widget, may default to returning the widget's -text option or some similar detail.

[tk]{.cmd} [accessible]{.sub} [set\_acc\_help]{.lit} [window]{.arg} [value]{.arg}
: String providing additional information on the widget and its purpose. For instance, can provide details on specific keyboard navigation for the widget.

[tk]{.cmd} [accessible]{.sub} [set\_acc\_name]{.lit} [window]{.arg} [value]{.arg}
: Short label identifying the widget. Defaults to the accessibility role.

[tk]{.cmd} [accessible]{.sub} [set\_acc\_role]{.lit} [window]{.arg} [value]{.arg}
: Assigns one of the following accessibility roles to a Tk widget: Button, Canvas, Checkbutton, Combobox, Entry, Label, Listbox, Menu, Notebook, Progressbar, Scale, Scrollbar, Spinbox, Table, Text, and Tree. Tk and ttk widgets having similar roles, such as a button and a ttk::button, will both be assigned the same accessibility role as these widgets are the same from an accessibility standpoint.

[tk]{.cmd} [accessible]{.sub} [set\_acc\_state]{.lit} [window]{.arg} [value]{.arg}
: ** Sets the widget's accessibility state, from one of the standard Tk widget states.

[tk]{.cmd} [accessible]{.sub} [set\_acc\_value]{.lit} [window]{.arg} [value]{.arg}
: Sets the widget's accessibility value. For instance, with a scale widget, will return the current numeric value of the scale.


# Additional notes

Some Tk GUI components, such as [frame]s and [panedwindow]s, are not included in the accessibility API because they are just containers and do not handle actions or data. Also, the canvas is not included in the accessibility API because it is a purely visual widget; the HTML5 [canvas] widget is not accessible to web API's for the same reason. Developers may choose to provide alternative data to provide information on the [canvas] widget, such as labels or help text. Finally, accessibility only runs in the main Tk interpreter, because of potential conflicts in child interpreters; also, accessibility is not supported under XQuartz (the X11 environment on macOS) because of too many conflicts between the X11 and macOS versions of accessibility.


[canvas]: canvas.md
[frame]: frame.md
[panedwindow]: panedwindow.md
[tk]: tk.md

