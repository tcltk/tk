---
CommandName: tk
title: tk
ManualSection: n
Version: 8.4
TclPart: Tk
TclDescription: Tk Built-In Commands
Links:
 - attribtable(n)
 - busy(n)
 - fontchooser(n)
 - print(n)
 - send(n)
 - sysnotify(n)
 - systray(n)
 - winfo(n)
Keywords:
 - application name
 - attribute
 - print
 - send
 - sysnotify
 - systray
Copyright:
 - Copyright (c) 1992 The Regents of the University of California.
 - Copyright (c) 1994-1996 Sun Microsystems, Inc.
---

# Name

tk - Manipulate Tk internal state

# Synopsis

::: {.synopsis} :::
[tk]{.cmd} [option]{.arg} [arg]{.optdot}
:::

# Description

The **tk** command provides access to miscellaneous elements of Tk's internal state. Most of the information manipulated by this command pertains to the application as a whole, or to a screen or display, rather than to a particular window. The command can take any of a number of different forms depending on the *option* argument.  The legal forms are:

[tk]{.cmd} [accessible]{.sub} [option]{.arg} [arg]{.optdot}
: This command makes Tk widgets accessible to screen readers. For more details see the [accessible] manual page.

[tk]{.cmd} [appname]{.sub} [newName]{.optarg}
: If *newName* is not specified, this command returns the name of the application (the name that may be used in [send] commands to communicate with the application). If *newName* is specified, then the name of the application is changed to *newName*. If the given name is already in use, then a suffix of the form "** #2**" or "** #3**" is appended in order to make the name unique. The command's result is the name actually chosen. *newName* should not start with a capital letter. This will interfere with option processing, since names starting with capitals are assumed to be classes;  as a result, Tk may not be able to find some options for the application. If sends have been disabled by deleting the [send] command, this command will reenable them and recreate the [send] command.

[tk]{.cmd} [attribtable]{.sub} [tableName]{.arg}
: This command creates an attribute table of the name *tableName* as a command in the namespace of the calling context if not fully qualified.  An attribute table is used to query and modify arbitrary data attached to any widget.  For more details see the [attribtable] manual page.

[tk]{.cmd} [busy]{.sub} [option]{.arg} [arg]{.optdot}
: This command controls the marking of window hierarchies as "busy", rendering them non-interactive while some other operation is proceeding.  For more details see the [busy] manual page.

[tk]{.cmd} [caret]{.sub} [window]{.arg} [[-x]{.lit} [x]{.arg}]{.optarg} [[-y]{.lit} [y]{.arg}]{.optarg} [[-height]{.lit} [height]{.arg}]{.optarg}
: Sets and queries the caret location for the display of the specified Tk window *window*.  The caret is the per-display cursor location used for indicating global focus (e.g. to comply with Microsoft Accessibility guidelines), as well as for location of the over-the-spot XIM (X Input Methods) or Windows IME windows.  If no options are specified, the last values used for setting the caret are return in option-value pair format.  **-x** and **-y** represent window-relative coordinates, and **-height** is the height of the current cursor location, or the height of the specified *window* if none is given.

[tk]{.cmd} [inactive]{.sub} [[-displayof]{.lit} [window]{.arg}]{.optarg} [reset]{.optlit}
: Returns a positive integer, the number of milliseconds since the last time the user interacted with the system.  If the **-displayof** option is given then the return value refers to the display of *window*; otherwise it refers to the display of the application's main window.

    **tk inactive** will return -1, if querying the user inactive time is not supported by the system, and in safe interpreters.

    If the literal string **reset** is given as an additional argument, the timer is reset and an empty string is returned.  Resetting the inactivity time is forbidden in safe interpreters and will throw an error if tried.

[tk]{.cmd} [fileicon]{.sub} [file]{.arg} [size]{.arg}
: Displays a system-native file icon.  For more details see the [fileicon] manual page.

[tk]{.cmd} [fontchooser]{.sub} [option]{.arg} [arg]{.optdot}
: Controls the Tk font selection dialog.  For more details see the [fontchooser] manual page.

[tk]{.cmd} [print]{.sub} [window]{.arg}
: The **tk print** command posts a dialog that allows users to print output from the [canvas] and [text] widgets.  The printing will be done using platform-native APIs and dialogs where available.  For more details see the [print] manual page.

[tk]{.cmd} [scaling]{.sub} [[-displayof]{.lit} [window]{.arg}]{.optarg} [number]{.optarg}
: Sets and queries the current scaling factor used by Tk to convert between physical units (for example, points, inches, or millimeters) and pixels.  The *number* argument is a floating point number that specifies the number of pixels per point on *window*'s display.  If the *window* argument is omitted, it defaults to the main window.  If the *number* argument is omitted, the current value of the scaling factor is returned.

    A "point" is a unit of measurement equal to 1/72 inch.  A scaling factor of 1.0 corresponds to 1 pixel per point, which is equivalent to a standard 72 dpi monitor.  A scaling factor of 1.25 would mean 1.25 pixels per point, which is the setting for a 90 dpi monitor; setting the scaling factor to 1.25 on a 72 dpi monitor would cause everything in the application to be displayed 1.25 times as large as normal.  The initial value for the scaling factor is set when the application starts, based on properties of the installed monitor, but it can be changed at any time.  Measurements made after the scaling factor is changed will use the new scaling factor, but it is undefined whether existing widgets will resize themselves dynamically to accommodate the new scaling factor.

[tk]{.cmd} [sysnotify]{.sub} [title]{.arg} [message]{.arg}
: The **tk sysnotify**  command creates a platform-specific system notification alert.  Its intent is to provide a brief, unobtrusive notification to the user by popping up a window that briefly appears in a corner of the screen.  For more details see the [sysnotify] manual page.

[tk]{.cmd} [systray]{.sub} [option]{.arg} [arg]{.optdot}
: The **tk systray** command creates an icon in the platform-specific tray.  For more details see the [systray] manual page.

[tk]{.cmd} [useinputmethods]{.sub} [[-displayof]{.lit} [window]{.arg}]{.optarg} [boolean]{.optarg}
: Sets and queries the state of whether Tk should use XIM (X Input Methods) for filtering events.  The resulting state is returned.  XIM is used in some locales (i.e., Japanese, Korean), to handle special input devices.  This feature is only significant on X.  If XIM support is not available, this will always return 0.  If the *window* argument is omitted, it defaults to the main window.  If the *boolean* argument is omitted, the current state is returned.  This is turned on by default for the main display.

[tk]{.cmd} [windowingsystem]{.sub}
: Returns the current Tk windowing system, one of **x11** (X11-based), **win32** (MS Windows), or **aqua** (macOS Aqua).



[accessible]: accessible.md
[attribtable]: attribtable.md
[busy]: busy.md
[canvas]: canvas.md
[fileicon]: fileicon.md
[fontchooser]: fontchooser.md
[print]: print.md
[send]: send.md
[sysnotify]: sysnotify.md
[systray]: systray.md
[text]: text.md

