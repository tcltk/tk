---
CommandName: tk attribtable
title: tk attribtable
ManualSection: n
Version: 9.1
TclPart: Tk
TclDescription: Tk Built-In Commands
Keywords:
 - widget
 - attribute
 - attribute table
Copyright:
 - Copyright (c) 2025 Csaba Nemethi
---

# Name

attribtable - Create an attribute table, used to query and modify arbitrary data attached to any widget.

# Synopsis

::: {.synopsis} :::
[tk]{.cmd} [attribtable]{.sub} [tableName]{.arg}
:::

# Description

This command creates an attribute table of the name *tableName*, implemented as a hash table and accessible as a command in the namespace of the calling context if not fully qualified, and returns the fully qualified name of the command just created.

An attribute table is used to query and modify arbitrary data attached to any widget.  These data are commonly called *attributes*.

If an attribute table of the given name already exists then it is replaced with the new one and all the attributes of all widgets set using the old table instance will be unset.

**REMARK 1:**  When the *tableName* command is deleted (via [rename] *tableName* "" or by deleting the containing namespace), all the attributes of all widgets set using this command are automatically unset and the underlying hash table is deleted.  This will free all the memory used by the table.

**REMARK 2:**  When a widget is destroyed, all of its attributes set by all attribute table commands are automatically unset.  This will free all the memory used by the widget's attributes.

The command *tableName* created by this command has the signature

```
tableName set|get|unset|clear|exists|names|pathnames args
```

In the description of the supported forms below, *pathName* specifies a widget whose attributes are being queried or modified via the *tableName* command.

[tableName]{.ins} [set]{.sub} [pathName]{.arg} [name]{.arg} [value]{.arg} [name value]{.optdot}
: Sets (i.e., adds or updates) the attributes identified by the *name* arguments to the values given by the *value* arguments.  Returns an empty string.  Example:

    ```
    # Save and then change the button's text
    tk attribtable table
    table set .btn prevText [.btn cget -text]
    .btn configure -text "NewText"
    ```

[tableName]{.ins} [get]{.sub} [pathName]{.arg} [name]{.optarg}
: see below ...

*tableName* **get** *pathName* ?*name defaultValue*?
: If *name* is specified then returns the corresponding attribute value, or an empty string or *defaultValue* (if given) if no corresponding value exists.  Otherwise returns a list consisting of all attribute names and values of the widget *pathName*.  Example:

    ```
    # Restore the button's previous text
    .btn configure -text [table get .btn prevText]
    ```

[tableName]{.ins} [unset]{.sub} [pathName]{.arg} [name]{.arg} [name]{.optdot}
: Unsets the attributes identified by the *name* arguments.  Returns an empty string.  Example:

    ```
    table unset .btn prevText
    ```

[tableName]{.ins} [clear]{.sub} [pathName]{.arg}
: Unsets all attributes and removes *pathName* from the list of those widgets that have attributes set via *tableName* [set].  Returns an empty string.  Example:

    ```
    table clear .btn
    ```

[tableName]{.ins} [exists]{.sub} [pathName]{.arg} [name]{.optarg}
: If the optional argument is present then returns **1** if the attribute identified by *name* exists and **0** otherwise.  Without the optional argument the return value is **1** if the widget *pathName* has at least one attribute set via *tableName* [set] and **0** otherwise. Example:

    ```
    if [table exists .btn prevText] {
        # Restore the button's previous text
        .btn configure -text [table get .btn prevText]
    }
    ```

[tableName]{.ins} [names]{.sub} [pathName]{.arg}
: Returns a list consisting of all attribute names of the widget *pathName*.  Example:

    ```
    puts "attribute names for .btn: [table names .btn]"
    ```

[tableName]{.ins} [pathnames]{.sub}
: Returns a list consisting of the path names of all widgets that have attributes set via *tableName* [set]. Example:

    ```
    puts "widgets in table: [table pathnames]"
    ```



[rename]: ../Tcl/rename.md
[set]: ../Tcl/set.md

