---
CommandName: bell
title: bell
ManualSection: n
Version: 8.4
TclPart: Tk
TclDescription: Tk Built-In Commands
Keywords:
 - beep
 - bell
 - ring
Copyright:
 - Copyright (c) 1994 The Regents of the University of California.
 - Copyright (c) 1994-1996 Sun Microsystems, Inc.
 - Copyright (c) 2000 Ajuba Solutions.
---

# Name

bell - Ring a display's bell

# Synopsis

::: {.synopsis} :::
[bell]{.cmd} [[-displayof]{.lit} [window]{.arg}]{.optarg} [-nice]{.optlit}
:::

# Description

This command rings the bell on the display for *window* and returns an empty string. If the **-displayof** option is omitted, the display of the application's main window is used by default. The command uses the current bell-related settings for the display, which may be modified with programs such as **xset**.

If **-nice** is not specified, this command also resets the screen saver for the screen.  Some screen savers will ignore this, but others will reset so that the screen becomes visible again.

