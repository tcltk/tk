---
CommandName: selection
title: selection
ManualSection: n
Version: 8.1
TclPart: Tk
TclDescription: Tk Built-In Commands
Links:
 - clipboard(n)
Keywords:
 - clear
 - format
 - handler
 - ICCCM
 - own
 - selection
 - target
 - type
Copyright:
 - Copyright (c) 1990-1994 The Regents of the University of California.
 - Copyright (c) 1994-1996 Sun Microsystems, Inc.
---

# Name

selection - Manipulate the X selection

# Synopsis

::: {.synopsis} :::
[selection]{.cmd} [option]{.arg} [arg]{.optdot}
:::

# Description

This command provides a Tcl interface to the X selection mechanism and implements the full selection functionality described in the X Inter-Client Communication Conventions Manual (ICCCM).

Note that for management of the **CLIPBOARD** selection (see below), the [clipboard] command may also be used.

The first argument to **selection** determines the format of the rest of the arguments and the behavior of the command.  The following forms are currently supported:

[selection]{.cmd} [clear]{.sub} [[-displayof]{.lit} [window]{.arg}]{.optarg} [[-selection]{.lit} [selection]{.arg}]{.optarg}
: If *selection* exists anywhere on *window*'s display, clear it so that no window owns the selection anymore.  *Selection* specifies the X selection that should be cleared, and should be an atom name such as **PRIMARY** or **CLIPBOARD**; see the Inter-Client Communication Conventions Manual for complete details. *Selection* defaults to **PRIMARY** and *window* defaults to ".". Returns an empty string.

[selection]{.cmd} [get]{.sub} [[-displayof]{.lit} [window]{.arg}]{.optarg} [[-selection]{.lit} [selection]{.arg}]{.optarg} [[-type]{.lit} [type]{.arg}]{.optarg}
: Retrieves the value of *selection* from *window*'s display and returns it as a result.  *Selection* defaults to **PRIMARY** and *window* defaults to ".". *Type* specifies the form in which the selection is to be returned (the desired "target" for conversion, in ICCCM terminology), and should be an atom name such as **STRING** or **FILE\_NAME**; see the Inter-Client Communication Conventions Manual for complete details. *Type* defaults to **STRING**.  The selection owner may choose to return the selection in any of several different representation formats, such as **STRING**, **UTF8\_STRING**, **ATOM**, **INTEGER**, etc. (this format is different than the selection type; see the ICCCM for all the confusing details). If the selection is returned in a non-string format, such as **INTEGER** or **ATOM**, the **selection** command converts it to string format as a collection of fields separated by spaces: atoms are converted to their textual names, and anything else is converted to hexadecimal integers. Note that **selection get** does not retrieve the selection in the **UTF8\_STRING** format unless told to.

[selection]{.cmd} [handle]{.sub} [[-selection]{.lit} [s]{.arg}]{.optarg} [[-type]{.lit} [t]{.arg}]{.optarg} [[-format]{.lit} [f]{.arg}]{.optarg} [window]{.arg} [command]{.arg}
: Creates a handler for selection requests, such that *command* will be executed whenever selection *s* is owned by *window* and someone attempts to retrieve it in the form given by type *t* (e.g. *t* is specified in the **selection get** command). *S* defaults to **PRIMARY**, *t* defaults to **STRING**, and *f* defaults to **STRING**.  If *command* is an empty string then any existing handler for *window*, *t*, and *s* is removed. Note that when the selection is handled as type **STRING** it is also automatically handled as type **UTF8\_STRING** as well.

    When *selection* is requested, *window* is the selection owner, and *type* is the requested type, *command* will be executed as a Tcl command with two additional numbers appended to it (with space separators). The two additional numbers are *offset* and *maxChars*:  *offset* specifies a starting character position in the selection and *maxChars* gives the maximum number of characters to retrieve.  The command should return a value consisting of at most *maxChars* of the selection, starting at position *offset*.  For very large selections (larger than *maxChars*) the selection will be retrieved using several invocations of *command* with increasing *offset* values.  If *command* returns a string whose length is less than *maxChars*, the return value is assumed to include all of the remainder of the selection;  if the length of *command*'s result is equal to *maxChars* then *command* will be invoked again, until it eventually returns a result shorter than *maxChars*.  The value of *maxChars* will always be relatively large (thousands of characters).

    If *command* returns an error then the selection retrieval is rejected just as if the selection did not exist at all.

    The *format* argument specifies the representation that should be used to transmit the selection to the requester (the second column of Table 2 of the ICCCM), and defaults to **STRING**.  If *format* is **STRING**, the selection is transmitted as 8-bit ASCII characters (i.e. just in the form returned by *command*, in the system [encoding]; the **UTF8\_STRING** format always uses UTF-8 as its encoding). If *format* is **ATOM**, then the return value from *command* is divided into fields separated by white space;  each field is converted to its atom value, and the 32-bit atom value is transmitted instead of the atom name. For any other *format*, the return value from *command* is divided into fields separated by white space and each field is converted to a 32-bit integer;  an array of integers is transmitted to the selection requester.

    The *format* argument is needed only for compatibility with selection requesters that do not use Tk.  If Tk is being used to retrieve the selection then the value is converted back to a string at the requesting end, so *format* is irrelevant.

[selection]{.cmd} [own]{.sub} [[-displayof]{.lit} [window]{.arg}]{.optarg} [[-selection]{.lit} [selection]{.arg}]{.optarg}
: see below ...

[selection]{.cmd} [own]{.sub} [[-command]{.lit} [command]{.arg}]{.optarg} [[-selection]{.lit} [selection]{.arg}]{.optarg} [window]{.arg}
: The first form of **selection own** returns the path name of the window in this application that owns *selection* on the display containing *window*, or an empty string if no window in this application owns the selection.  *Selection* defaults to **PRIMARY** and *window* defaults to ".".

    The second form of **selection own** causes *window* to become the new owner of *selection* on *window*'s display, returning an empty string as result. The existing owner, if any, is notified that it has lost the selection. If *command* is specified, it is a Tcl script to execute when some other window claims ownership of the selection away from *window*.  *Selection* defaults to PRIMARY.


# Widget facilities

The [text], [entry], [ttk::entry][ttk_entry], [listbox], [spinbox] and [ttk::spinbox][ttk_spinbox] widgets have the option **-exportselection**.  If a widget has this option set to boolean **true**, then (in an unsafe interpreter) a selection made in the widget is automatically written to the **PRIMARY** selection.

A GUI event, for example **<<PasteSelection>>**, can copy the **PRIMARY** selection to certain widgets.  This copy is implemented by a widget binding to the event.  The binding script makes appropriate calls to the **selection** command.

# Portability issues

On X11, the **PRIMARY** selection is a system-wide feature of the X server, allowing communication between different processes that are X11 clients.

On Windows, the **PRIMARY** selection is not provided by the system, but only by Tk, and so it is shared only between windows of a parent interpreter and its child interpreters.  It is not shared between interpreters in different processes or different threads.  Each parent interpreter has a separate **PRIMARY** selection that is shared only with its child interpreters which are not safe interpreters.

# Security

A safe interpreter cannot read from the **PRIMARY** selection because its **selection** command is hidden.  For this reason the **PRIMARY** selection cannot be written to the Tk widgets of a safe interpreter.

A Tk widget can have its option **-exportselection** set to boolean **true**, but in a safe interpreter this option has no effect: writing from the widget to the **PRIMARY** selection is disabled.

These are security features.  A safe interpreter may run untrusted code, and it is a security risk if this untrusted code can read or write the **PRIMARY** selection used by other interpreters.

# Examples

On X11 platforms, one of the standard selections available is the **SECONDARY** selection. Hardly anything uses it, but here is how to read it using Tk:

```
set selContents [selection get -selection SECONDARY]
```

Many different types of data may be available for a selection; the special type **TARGETS** allows you to get a list of available types:

```
foreach type [selection get -type TARGETS] {
   puts "Selection PRIMARY supports type $type"
}
```

To claim the selection, you must first set up a handler to supply the data for the selection. Then you have to claim the selection...

```
# Set up the data handler ready for incoming requests
set foo "This is a string with some data in it... blah blah"
selection handle -selection SECONDARY . getData
proc getData {offset maxChars} {
   puts "Retrieving selection starting at $offset"
   return [string range $::foo $offset [expr {$offset+$maxChars-1}]]
}

# Now we grab the selection itself
puts "Claiming selection"
selection own -command lost -selection SECONDARY .
proc lost {} {
   puts "Lost selection"
}
```


[clipboard]: clipboard.md
[encoding]: ../Tcl/encoding.md
[entry]: entry.md
[listbox]: listbox.md
[spinbox]: spinbox.md
[text]: text.md
[ttk_entry]: ttk_entry.md
[ttk_spinbox]: ttk_spinbox.md

