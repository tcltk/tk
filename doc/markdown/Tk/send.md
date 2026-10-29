---
CommandName: send
title: send
ManualSection: n
Version: 4.0
TclPart: Tk
TclDescription: Tk Built-In Commands
Keywords:
 - application
 - dde
 - name
 - remote execution
 - security
 - send
Copyright:
 - Copyright (c) 1990-1994 The Regents of the University of California.
 - Copyright (c) 1994-1996 Sun Microsystems, Inc.
---

# Name

send - Execute a command in a different application

# Synopsis

::: {.synopsis} :::
[send]{.cmd} [options]{.optarg} [app]{.arg} [cmd]{.arg} [arg]{.optdot}
:::

# Description

This command arranges for *cmd* (and *arg*s) to be executed in the application named by *app*.  It returns the result or error from that command execution. *App* may be the name of any application whose main window is on the display containing the sender's main window;  it need not be within the same process. If no *arg* arguments are present, then the command to be executed is contained entirely within the *cmd* argument.  If one or more *arg*s are present, they are concatenated to form the command to be executed, just as for the [eval] command.

If the initial arguments of the command begin with "-" they are treated as options.  The following options are currently defined:

[-async]{.lit}
: Requests asynchronous invocation.  In this case the **send** command will complete immediately without waiting for *cmd* to complete in the target application;  no result will be available and errors in the sent command will be ignored. If the target application is in the same process as the sending application then the **-async** option is ignored.

[-displayof]{.lit} [pathName]{.arg}
: Specifies that the target application's main window is on the display of the window given by *pathName*, instead of the display containing the application's main window.

[--]{.lit}
: Serves no purpose except to terminate the list of options.  This option is needed only if *app* could contain a leading "-" character.


# Application names

The name of an application is set initially from the name of the program or script that created the application. You can query and change the name of an application with the [tk appname][tk] command.

# Disabling sends

If the **send** command is removed from an application (e.g. with the command [rename] **send {}**) then the application will not respond to incoming send requests anymore,  nor will it be able to issue outgoing requests. Communication can be reenabled by invoking the [tk appname][tk] command.

# Security

The **send** command is potentially a serious security loophole. On Unix, any application that can connect to your X server can send scripts to your applications. These incoming scripts can use Tcl to read and write your files and invoke subprocesses under your name. Host-based access control such as that provided by **xhost** is particularly insecure, since it allows anyone with an account on particular hosts to connect to your server, and if disabled it allows anyone anywhere to connect to your server. In order to provide at least a small amount of security, Tk checks the access control being used by the server and rejects incoming sends unless (a) **xhost**-style access control is enabled (i.e. only certain hosts can establish connections) and (b) the list of enabled hosts is empty. This means that applications cannot connect to your server unless they use some other form of authorization such as that provide by **xauth**.

Under macOS/aqua, the **send** command can target an interpreter running in a different process, but only if the target process is owned by the same user and is running on the same host system as the sender's process.  The "winfo interps" command shows exactly which interpreters are valid targets for the **send** command.

Under Windows, **send** is currently disabled.  Most of the functionality is provided by the **dde** command instead.

# Example

This script fragment can be used to make an application that only runs once on a particular display.

```
if {[tk appname FoobarApp] ne "FoobarApp"} {
    send -async FoobarApp RemoteStart $argv
    exit
}
# The command that will be called remotely, which raises
# the application main window and opens the requested files
proc RemoteStart args {
    raise .
    foreach filename $args {
        OpenFile $filename
    }
}
```


[eval]: ../Tcl/eval.md
[rename]: ../Tcl/rename.md
[tk]: tk.md

