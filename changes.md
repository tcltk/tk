
The source code for Tk is managed by fossil.  Tk developers coordinate all
changes to the Tk source code at

> [Tk Source Code](https://core.tcl-lang.org/tk/)

Release Tk 9.0.1 arises from the check-in with tag `core-9-0-1`.

## Changes since Tk 9.0.0
 - [Canvas widget handles pixel objects incorrectly in Tk 9.0](https://core.tcl-lang.org/tk/tktview/610a73)
 - [SIGABRT from Tk_DeleteErrorHandler()](https://core.tcl-lang.org/tk/tktview/f52986)
 - [Tilde file syntax not available on 9.0 but used by "~/.Xdefaults"](https://core.tcl-lang.org/tk/tktview/fcfddc)
 - [leftover use of tilde in filename string](https://core.tcl-lang.org/tk/tktview/767702)
 - [build failure on macOS < 10.13](https://core.tcl-lang.org/tk/tktview/d48cbf)
 - [Two potentially bogus binding scripts for <TouchpadScroll>](https://core.tcl-lang.org/tk/tktview/73c5e3)
 - [Aqua: canvas items are not always redrawn](https://core.tcl-lang.org/tk/tktview/5869c2)
 - [Aqua: color rgb values do not behave as expected when appearance is changed](https://core.tcl-lang.org/tk/tktview/01f58b)
 - [Aqua: winfo rgb . systemLabelColor returns a weird result on aqua](https://core.tcl-lang.org/tk/tktview/23b57a)
 - [Aqua: background thread became slower](https://core.tcl-lang.org/tk/tktview/547cc6)
 - [Use of Tcl_Obj vs char * in Widget storage](https://core.tcl-lang.org/tk/tktview/f91aa2)
 - [cannot build .chm help file (Windows)](https://core.tcl-lang.org/tk/tktview/bb110c)

Release Tk 9.0.0 arises from the check-in with tag `core-9-0-0`.

Highlighted differences between Tk 9.0 and Tk 8.6 are summarized below,
with focus on changes important to programmers using the Tk library and
writing Tcl scripts containing Tk commands.

## Many improvements to use of platform features and conventions.
 - Built-in widgets and themes are scaling-aware.
 - Improved support of two-finger gestures, where available
 - The `tk windowingsystem` "aqua" needs macOS 10.9 or later

## New commands and options
 - `tk sysnotify` — Access to the OS notifications system
 - `tk systray` — Access to the OS tray facility
 - `tk print` — Access to the OS printing facility

## Widget options
 - New `ttk::progressbar` option: **-text**
 - `$frame ... -backgroundimage $img -tile $bool`
 - `$menu id`, `$menu add|insert ... ?$id? ...`
 - `$image get ... -withalpha ...`
 - All indices now accept the forms **end**, **end-int**, **int+|-int**

## Improved widget appearance
 - `ttk::notebook` with nondefault tab positions

## Images
 - Partial SVG support
 - Read/write access to photo image metadata

## Known bugs
 - [many PIXEL options don't keep their configured value](https://core.tcl-lang.org/tk/tktview/29ba53)
 - [Inconsistent reporting of child geometry changes to grid container](https://core.tcl-lang.org/tk/tktview/beaa8e)
 - [Inconsistency in whether widgets allow negative borderwidths](https://core.tcl-lang.org/tk/tktview/5f739d)
 - [Enter key works differently in Windows and Linux](https://core.tcl-lang.org/tk/tktview/b3a1b9)
 - [slow widget creation if default font is not used](https://core.tcl-lang.org/tk/tktview/8da7af)
 - [The wm manage command does not work on current macOS versions](https://core.tcl-lang.org/tk/tktview/8a6012)
 - [Slow processing irregular transparencies](https://core.tcl-lang.org/tk/tktview/919066)
 - [text's cursor width on 0th column](https://core.tcl-lang.org/tk/tktview/47fbfc)
 - [text widget breaks graphemes with combining diacritical marks](https://core.tcl-lang.org/tk/tktview/442208)

