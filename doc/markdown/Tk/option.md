---
CommandName: option
title: option
ManualSection: n
Version: unknown
TclPart: Tk
TclDescription: Tk Built-In Commands
Links:
 - options(n)
 - wish(1)
Keywords:
 - database
 - option
 - priority
 - retrieve
Copyright:
 - Copyright (c) 1990 The Regents of the University of California.
 - Copyright (c) 1994-1996 Sun Microsystems, Inc.
---

# Name

option - Add/retrieve window options to/from the option database

# Synopsis

::: {.synopsis} :::
[option]{.cmd} [add]{.sub} [pattern]{.arg} [value]{.arg} [priority]{.optarg}
[option]{.cmd} [clear]{.sub}
[option]{.cmd} [get]{.sub} [window]{.arg} [name]{.arg} [class]{.arg}
[option]{.cmd} [readfile]{.sub} [fileName]{.arg} [priority]{.optarg}
:::

# Description

The **option** command allows you to add entries to the Tk option database or to retrieve options from the database. The **add** form of the command adds a new option to the database. *Pattern* contains the option being specified, and consists of names and/or classes separated by asterisks or dots, in the usual X format (see [Pattern format]).  *Value* contains a text string to associate with *pattern*;  this is the value that will be returned in calls to [Tk\_GetOption][GetOption] or by invocations of the **option get** command.  If *priority* is specified, it indicates the priority level for this option (see below for legal values);  it defaults to **interactive**. This command always returns an empty string.

The **option clear** command clears the option database.  Default options (from the **RESOURCE\_MANAGER** property or the **.Xdefaults** file) will be reloaded automatically the next time an option is added to the database or removed from it.  This command always returns an empty string.

The **option get** command returns the value of the option specified for *window* under *name* and *class*.  If several entries in the option database match *window*, *name*, and *class*, then the command returns whichever was created with highest *priority* level.  If there are several matching entries at the same priority level, then it returns whichever entry was most recently entered into the option database.  If there are no matching entries, then the empty string is returned.

The **readfile** form of the command reads *fileName*, which should have the standard format for an X resource database such as **.Xdefaults**, and adds all the options specified in that file to the option database.  If *priority* is specified, it indicates the priority level at which to enter the options;  *priority* defaults to **interactive**.

In the values in the file, the escape sequences of the X resource file syntax are recognized: a backslash followed by a newline is removed (so that the value continues on the next line), **\\n** is replaced by a newline, a backslash followed by a space or a tab is replaced by that character, **\\\\** by a single backslash, and a backslash followed by three octal digits by the byte with that value (so a character outside ASCII can be written as the octal escapes of its UTF-8 bytes). Therefore a value which contains backslashes or newlines, such as a Tcl list, must be escaped before it is written to the file, for example:

```
puts $file "*items: [string map {\\ \\\\ \n \\n} $value]"
```

The file is read through a channel which is in "utf-8" encoding, invalid byte sequences are automatically converted to valid ones. This means that encodings like ISO 8859-1 or cp1252 with high probability will work as well, but this cannot be guaranteed. This cannot be changed, setting the \[encoding system\] has no effect.

The *priority* arguments to the **option** command are normally specified symbolically using one of the following values:

**widgetDefault**
: Level 20.  Used for default values hard-coded into widgets.

**startupFile**
: Level 40.  Used for options specified in application-specific startup files.

**userDefault**
: Level 60.  Used for options specified in user-specific defaults files, such as **.Xdefaults**, resource databases loaded into the X server, or user-specific startup files.

**interactive**
: Level 80.  Used for options specified interactively after the application starts running.  If *priority* is not specified, it defaults to this level.


Any of the above keywords may be abbreviated.  In addition, priorities may be specified numerically using integers between 0 and 100, inclusive.  The numeric form is probably a bad idea except for new priority levels other than the ones given above.

# Pattern format

Patterns consist of a sequence of words separated by either periods, ".", or asterisks "\*". The overall pattern may also be optionally preceded by an asterisk.

Each word in the pattern conventionally starts with either an upper-case letter (in which case it denotes the class of either a widget or an option) or any other character, when it denotes the name of a widget or option. The last word in the pattern always indicates the option; the preceding ones constrain which widgets that option will be looked for in.

When two words are separated by a period, the latter widget must be a direct child of the former (or the option must apply to only the indicated widgets). When two words are separated by an asterisk, any depth of widgets may lie between the former and latter widgets (and the option applies to all widgets that are children of the former widget).

If the overall pattern is preceded by an asterisk, then the overall pattern applies anywhere it can throughout the whole widget hierarchy. Otherwise the first word of the pattern is matched against the name and class of the "**.**" [toplevel], which are usually set by options to [wish].

# Examples

Instruct every button in the application to have red text on it unless explicitly overridden, by setting the **foreground** for the **Button** class (note that on some platforms the option is ignored):

```
option add *Button.foreground red startupFile
```

Allow users to control what happens in an entry widget when the Return key is pressed by specifying a script in the option database and add a default option for that which rings the bell:

```
entry .e
bind .e <Return> [option get .e returnCommand Command]
option add *.e.returnCommand bell widgetDefault
```


[GetOption]: ../TkCAPI/GetOption.md
[toplevel]: toplevel.md
[wish]: wish.md

