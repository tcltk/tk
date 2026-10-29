---
CommandName: Safe Tk
title: Safe Tk
ManualSection: n
Version: 8.0
TclPart: Tk
TclDescription: Tk Built-In Commands
Links:
 - safe(n)
 - interp(n)
 - library(n)
 - load(n)
 - package(n)
 - source(n)
 - unknown(n)
Keywords:
 - alias
 - auto-loading
 - auto_mkindex
 - load
 - parent interpreter
 - safe
 - interpreter
 - child interpreter
 - source
Copyright:
 - Copyright (c) 1995-1996 Sun Microsystems, Inc.
---

# Name

safe::loadTk - Load Tk into a safe interpreter.

# Synopsis

::: {.synopsis} :::
[safe::loadTk]{.cmd} [child]{.arg} [[-use]{.lit} [windowId]{.arg}]{.optarg} [[-display]{.lit} [displayName]{.arg}]{.optarg}
:::

# Description

Safe Tk is based on Safe Tcl, which provides a mechanism that allows restricted and mediated access to auto-loading and packages for safe interpreters.  Safe Tk adds the ability to configure the interpreter for safe Tk operations and load Tk into safe interpreters.

The **safe::loadTk** command initializes the required data structures in the named safe interpreter and then loads Tk into it.  The interpreter must have been created with **safe::interpCreate** or have been initialized with **safe::interpInit**.  The command returns the name of the safe interpreter.  If **-use** is specified, the window identified by the specified system dependent identifier *windowId* is used to contain the "." window of the safe interpreter; it can be any valid id, eventually referencing a window belonging to another application. As a convenience, if the window you plan to use is a Tk Window of the application you can use the window name (e.g., "**.x.y**") instead of its window Id (e.g., from [winfo id][winfo] *.x.y*). When **-use** is not specified, a new toplevel window is created for the "." window of the safe interpreter. On X11 if you want the embedded window to use another display than the default one, specify it with **-display**.  See the [Security issues] section below for implementation details.

# Security issues

Please read the **safe** manual page for Tcl to learn about the basic security considerations for Safe Tcl.

**safe::loadTk** adds the value of [tk\_library][tkvars] taken from the parent interpreter to the virtual access path of the safe interpreter so that auto-loading will work in the safe interpreter.

Tk initialization is now safe with respect to not trusting the child's state for startup. **safe::loadTk** registers the child's name so when the Tk initialization ([Tk\_SafeInit][Tk_Init]) is called and in turn calls the parent's **safe::InitTk** it will return the desired **argv** equivalent (**-use** *windowId*, correct **-display**, etc.)

When **-use** is not used, the new toplevel created is specially decorated so the user is always aware that the user interface presented comes from a potentially unsafe code and can easily delete the corresponding interpreter.

On X11, conflicting **-use** and **-display** are likely to generate a fatal X error.


[Tk_Init]: ../TkCAPI/Tk_Init.md
[tkvars]: tkvars.md
[winfo]: winfo.md

