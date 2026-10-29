---
CommandName: tk sysnotify
title: tk sysnotify
ManualSection: n
Version: unknown
TclPart: Tk
TclDescription: Tk Built-In Commands
Keywords:
 - notify
 - alert
Copyright:
 - Copyright (c) 2020 Kevin Walzer
---

# Name

sysnotify - Creates a notification window with a title and message.

# Synopsis

::: {.synopsis} :::
[tk]{.cmd} [sysnotify]{.sub} [title]{.arg} [message]{.arg}
:::

# Description

The [tk sysnotify][tk] command creates a platform-specific system notification alert. Its intent is to provide a brief, unobtrusive notification to the user by popping up a window that briefly appears in a corner of the screen.

# Example

Here is an example of the [tk sysnotify][tk] code:

```
tk sysnotify "Alert" \
      "This is just a test of the Tk System Notification Code."
```

# Platform notes

The macOS and Windows versions are native implementations using system API's. The X11 version has a conditional dependency on libnotify, and falls back to a Tcl-only implementation if libnotify is not installed. On macOS and Linux, the notification includes a platform-specific default image to accompany the text. On Windows, the notification displays the same image as the one in the system tray; Windows images will display best if they are 32x32 pixels.

**macOS**
: The macOS version will request permission from the user to authorize notifications. This must be activated in Apple's System Preferences Notifications section.

    If deploying an application using the standalone version of Wish.app, setting the bundle ID in the applications Info.plist file to begin with "**com**" seems necessary for notifications to work. Using a different prefix for the bundle ID, such as something like "**tk.tcl.tkchat**", will cause notifications to silently fail.

**Windows**
: The image is taken from the system tray, i.e., **sysnotify** can only be called when a [systray] was installed.



[systray]: systray.md
[tk]: tk.md

