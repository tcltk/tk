---
CommandName: label
title: label
ManualSection: n
Version: 4.0
TclPart: Tk
TclDescription: Tk Built-In Commands
Links:
 - labelframe(n)
 - button(n)
 - ttk::label(n)
Keywords:
 - label
 - widget
Copyright:
 - Copyright (c) 1990-1994 The Regents of the University of California.
 - Copyright (c) 1994-1996 Sun Microsystems, Inc.
---

# Name

label - Create and manipulate 'label' non-interactive text or image widgets

# Synopsis

::: {.synopsis} :::
[label]{.cmd} [pathName]{.arg} [options]{.optarg}
:::

# Standard options

::: {.standardoptions} :::
-activebackground
-activeforeground
-anchor
-background
-bitmap
-borderwidth
-compound
-cursor
-disabledforeground
-font
-foreground
-highlightbackground
-highlightcolor
-highlightthickness
-image
-justify
-padx
-pady
-relief
-takefocus
-text
-textangle
-textvariable
-underline
-wraplength
:::

See the [options] manual entry for details on the standard options.

# Widget-specific options

[-height]{.cmdline} [height]{.dbname} [Height]{.dbclass}
: Specifies a desired height for the label. If an image or bitmap is being displayed in the label then the value is in screen units (i.e. any of the forms acceptable to [Tk\_GetPixels][GetPixels]); for text it is in lines of text. If this option is not specified, the label's desired height is computed from the size of the image or bitmap or text being displayed in it.

[-state]{.cmdline} [state]{.dbname} [State]{.dbclass}
: Specifies one of three states for the label:  **normal**, **active**, or **disabled**.  In normal state the button is displayed using the **-foreground** and **-background** options.  In active state the label is displayed using the **-activeforeground** and **-activebackground** options.  In the disabled state the **-disabledforeground** and **-background** options determine how the button is displayed.

[-width]{.cmdline} [width]{.dbname} [Width]{.dbclass}
: Specifies a desired width for the label. If an image or bitmap is being displayed in the label then the value is in screen units (i.e. any of the forms acceptable to [Tk\_GetPixels][GetPixels]); for text it is in characters. If this option is not specified, the label's desired width is computed from the size of the image or bitmap or text being displayed in it.


# Description

The **label** command creates a new window (given by the *pathName* argument) and makes it into a label widget. Additional options, described above, may be specified on the command line or in the option database to configure aspects of the label such as its colors, font, text, and initial relief.  The **label** command returns its *pathName* argument.  At the time this command is invoked, there must not exist a window named *pathName*, but *pathName*'s parent must exist.

A label is a widget that displays a textual string, bitmap or image. If text is displayed, it must all be in a single font, but it can occupy multiple lines on the screen (if it contains newlines or if wrapping occurs because of the **-wraplength** option) and one of the characters may optionally be underlined using the **-underline** option. The label can be manipulated in a few simple ways, such as changing its relief or text, using the commands described below.

# Widget command

The **label** command creates a new Tcl command whose name is *pathName*.  This command may be used to invoke various operations on the widget.  It has the following general form:

```
pathName option ?arg ...?
```

*Option* and the *arg*s determine the exact behavior of the command.  The following commands are possible for label widgets:

[pathName]{.ins} [cget]{.sub} [option]{.arg}
: Returns the current value of the configuration option given by *option*. *Option* may have any of the values accepted by the **label** command.

[pathName]{.ins} [configure]{.sub} [option]{.optarg} [value option value]{.optdot}
: Query or modify the configuration options of the widget. If no *option* is specified, returns a list describing all of the available options for *pathName* (see [Tk\_ConfigureInfo][ConfigWidg] for information on the format of this list).  If *option* is specified with no *value*, then the command returns a list describing the one named option (this list will be identical to the corresponding sublist of the value returned if no *option* is specified).  If one or more *option-value* pairs are specified, then the command modifies the given widget option(s) to have the given value(s);  in this case the command returns an empty string. *Option* may have any of the values accepted by the **label** command.


# Bindings

When a new label is created, it has no default event bindings: labels are not intended to be interactive.

# Example

```
# Make the widgets
label .t -text "This widget is at the top"    -bg red
label .b -text "This widget is at the bottom" -bg green
label .l -text "Left\nHand\nSide"
label .r -text "Right\nHand\nSide"
text .mid
.mid insert end "This layout is like Java's BorderLayout"
# Lay them out
pack .t   -side top    -fill x
pack .b   -side bottom -fill x
pack .l   -side left   -fill y
pack .r   -side right  -fill y
pack .mid -expand 1    -fill both
```


[ConfigWidg]: ../TkCAPI/ConfigWidg.md
[GetPixels]: ../TkCAPI/GetPixels.md
[options]: options.md

