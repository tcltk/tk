---
CommandName: message
title: message
ManualSection: n
Version: 4.0
TclPart: Tk
TclDescription: Tk Built-In Commands
Links:
 - label(n)
Keywords:
 - message
 - widget
Copyright:
 - Copyright (c) 1990-1994 The Regents of the University of California.
 - Copyright (c) 1994-1996 Sun Microsystems, Inc.
---

# Name

message - Create and manipulate 'message' non-interactive text widgets

# Synopsis

::: {.synopsis} :::
[message]{.cmd} [pathName]{.arg} [options]{.optarg}
:::

# Standard options

::: {.standardoptions} :::
-anchor
-background
-borderwidth
-cursor
-font
-foreground
-highlightbackground
-highlightcolor
-highlightthickness
-padx
-pady
-relief
-takefocus
-text
-textvariable
:::

See the [options] manual entry for details on the standard options.

# Widget-specific options

[-aspect]{.cmdline} [aspect]{.dbname} [Aspect]{.dbclass}
: Specifies a non-negative integer value indicating desired aspect ratio for the text.  The aspect ratio is specified as 100\*width/height.  100 means the text should be as wide as it is tall, 200 means the text should be twice as wide as it is tall, 50 means the text should be twice as tall as it is wide, and so on. Used to choose line length for text if **-width** option is not specified. Defaults to 150.

[-justify]{.cmdline} [justify]{.dbname} [Justify]{.dbclass}
: Specifies how to justify lines of text. Must be one of **left**, **center**, or **right**.  Defaults to **left**. This option works together with the **-anchor**, **-aspect**, **-padx**, **-pady**, and **-width** options to provide a variety of arrangements of the text within the window. The **-aspect** and **-width** options determine the amount of screen space needed to display the text. The **-anchor**, **-padx**, and **-pady** options determine where this rectangular area is displayed within the widget's window, and the **-justify** option determines how each line is displayed within that rectangular region. For example, suppose **-anchor** is **e** and **-justify** is **left**, and that the message window is much larger than needed for the text. The text will be displayed so that the left edges of all the lines line up and the right edge of the longest line is **-padx** from the right side of the window;  the entire text block will be centered in the vertical span of the window.

[-width]{.cmdline} [width]{.dbname} [Width]{.dbclass}
: Specifies the length of lines in the window. The value may have any of the forms acceptable to [Tk\_GetPixels][GetPixels]. If this option has a value greater than zero then the **-aspect** option is ignored and the **-width** option determines the line length. If this option value is negative or zero, then the **-aspect** option determines the line length.


# Description

The **message** command creates a new window (given by the *pathName* argument) and makes it into a message widget. Additional options, described above, may be specified on the command line or in the option database to configure aspects of the message such as its colors, font, text, and initial relief.  The **message** command returns its *pathName* argument.  At the time this command is invoked, there must not exist a window named *pathName*, but *pathName*'s parent must exist.

A message is a widget that displays a textual string.  A message widget has three special features that differentiate it from a [label] widget.  First, it breaks up its string into lines in order to produce a given aspect ratio for the window.  The line breaks are chosen at word boundaries wherever possible (if not even a single word would fit on a line, then the word will be split across lines).  Newline characters in the string will force line breaks;  they can be used, for example, to leave blank lines in the display.

The second feature of a message widget is justification.  The text may be displayed left-justified (each line starts at the left side of the window), centered on a line-by-line basis, or right-justified (each line ends at the right side of the window).

The third feature of a message widget is that it handles control characters and non-printing characters specially.  Tab characters are replaced with enough blank space to line up on the next 8-character boundary.  Newlines cause line breaks.  Other control characters (ASCII code less than 0x20) and characters not defined in the font are displayed as a four-character sequence **\\x***hh* where *hh* is the two-digit hexadecimal number corresponding to the character.  In the unusual case where the font does not contain all of the characters in "0123456789abcdef\\x" then control characters and undefined characters are not displayed at all.

# Widget command

The **message** command creates a new Tcl command whose name is *pathName*.  This command may be used to invoke various operations on the widget.  It has the following general form:

```
pathName option ?arg ...?
```

*Option* and the *arg*s determine the exact behavior of the command.  The following commands are possible for message widgets:

[pathName]{.ins} [cget]{.sub} [option]{.arg}
: Returns the current value of the configuration option given by *option*. *Option* may have any of the values accepted by the **message** command.

[pathName]{.ins} [configure]{.sub} [option]{.optarg} [value option value]{.optdot}
: Query or modify the configuration options of the widget. If no *option* is specified, returns a list describing all of the available options for *pathName* (see [Tk\_ConfigureInfo][ConfigWidg] for information on the format of this list).  If *option* is specified with no *value*, then the command returns a list describing the one named option (this list will be identical to the corresponding sublist of the value returned if no *option* is specified).  If one or more *option-value* pairs are specified, then the command modifies the given widget option(s) to have the given value(s);  in this case the command returns an empty string. *Option* may have any of the values accepted by the **message** command.


# Default bindings

When a new message is created, it has no default event bindings: messages are intended for output purposes only.

# Bugs

Tabs do not work very well with text that is centered or right-justified. The most common result is that the line is justified wrong.


[ConfigWidg]: ../TkCAPI/ConfigWidg.md
[GetPixels]: ../TkCAPI/GetPixels.md
[label]: label.md
[options]: options.md

