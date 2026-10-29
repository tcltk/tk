---
CommandName: tkerror
title: tkerror
ManualSection: n
Version: 4.1
TclPart: Tk
TclDescription: Tk Built-In Commands
Keywords:
 - background error
 - reporting
Copyright:
 - Copyright (c) 1990-1994 The Regents of the University of California.
 - Copyright (c) 1994-1996 Sun Microsystems, Inc.
---

# Name

tkerror - Command invoked to process background errors

# Synopsis

::: {.synopsis} :::
[tkerror]{.cmd} [message]{.arg}
:::

# Description

Note: as of Tk 4.1 the **tkerror** command has been renamed to [bgerror] because the event loop (which is what usually invokes it) is now part of Tcl.  Using the **tkerror** name is deprecated.

For backward compatibility the [bgerror] provided by the current Tk version still tries to call **tkerror** if there is one (or an auto loadable one), so old script defining that error handler should still work, but you should anyhow modify your scripts to use [bgerror] instead of **tkerror** because that support for the old name might vanish in the future. If that call fails, [bgerror] posts a dialog showing the error and offering to see the stack trace to the user. If you want your own error management you should directly override [bgerror] instead of **tkerror**. Documentation for [bgerror] is available as part of Tcl's documentation.


[bgerror]: ../Tcl/bgerror.md

