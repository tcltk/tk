
The source code for Tk is managed by fossil.  Tk developers coordinate all
changes to the Tk source code at

> [Tk Source Code](https://core.tcl-lang.org/tk/)

Release Tk 9.0.2 arises from the check-in with tag `core-9-0-2`.

Tk 9.0.2 continues the Tk 9.0 series of releases.  The Tk 9.0 series
does not support Tcl 8.6.  The Tk 9.0 series extends the Tcl 9.0 series.
To make use of Tk 9.0.2, first a Tcl 9.0 release must be present.
As new Tk features are developed, expect them to appear in Tk 9, but not
necessarily in Tk 8.

Tk patch releases have the primary purpose of delivering bug fixes
to the userbase.

# Bug fixes
 - [inaccurate scrollbar error-message](https://core.tcl-lang.org/tk/tktview/f88118)
 - [Build tk 9.0.1 failed on macos 10.13](https://core.tcl-lang.org/tk/tktview/cb5d77)
 - [image svg upstream out of bound read nanosvg#262](https://core.tcl-lang.org/tk/tktview/121786)
 - [wm iconbitmap does not correctly set the icon pixmap hint on macOS](https://core.tcl-lang.org/tk/tktview/13ac26)
 - [Backspace crashes 9.0 interpreter on FreeBSD](https://core.tcl-lang.org/tk/tktview/1da19a)
 - [Bug in the ttk::scale widget of the "default" theme](https://core.tcl-lang.org/tk/tktview/126d07)
 - [Wrong appearance of the ttk::menubutton indicator of the "xpnative" theme](https://core.tcl-lang.org/tk/tktview/525536)
 - [English shortcuts for Chinese locale](https://core.tcl-lang.org/tk/tktview/c99266)
 - [No grip element in ttk::panedwindow sashes of most built-in themes](https://core.tcl-lang.org/tk/tktview/9902d8)
 - [Tk_Get3DBorderColors broken by design](https://core.tcl-lang.org/tk/tktview/517165)

Release Tk 9.0.1 arises from the check-in with tag `core-9-0-1`.

Tk 9.0.1 continues the Tk 9.0 series of releases.  The Tk 9.0 series
does not support Tcl 8.6.  The Tk 9.0 series extends the Tcl 9.0 series.
To make use of Tk 9.0.1, first a Tcl 9.0 release must be present.
As new Tk features are developed, expect them to appear in Tk 9, but not
necessarily in Tk 8.

Tk patch releases have the primary purpose of delivering bug fixes
to the userbase.  As the first patch release in the Tk 9.0 series,
Tk 9.0.1 also includes a small number of interface changes that complete
some incomplete features first delivered in Tk 9.0.0.

# Completed 9.0 Features and Interfaces
 - [TIP #706: Expose three Tk "In Context" functions via stubs table](https://core.tcl-lang.org/tips/doc/trunk/tip/706.md)
 - [Tilde file syntax not available on 9.0 but used by "~/.Xdefaults"](https://core.tcl-lang.org/tk/tktview/fcfddc)
 - [leftover use of tilde in filename string](https://core.tcl-lang.org/tk/tktview/767702)

# Bug fixes
 - [Canvas widget handles pixel objects incorrectly in Tk 9.0](https://core.tcl-lang.org/tk/tktview/610a73)
 - [SIGABRT from Tk_DeleteErrorHandler()](https://core.tcl-lang.org/tk/tktview/f52986)
 - [build failure on macOS < 10.13](https://core.tcl-lang.org/tk/tktview/d48cbf)
 - [Two potentially bogus binding scripts for <TouchpadScroll>](https://core.tcl-lang.org/tk/tktview/73c5e3)
 - [Aqua: canvas items are not always redrawn](https://core.tcl-lang.org/tk/tktview/5869c2)
 - [Aqua: color rgb values do not behave as expected when appearance is changed](https://core.tcl-lang.org/tk/tktview/01f58b)
 - [Aqua: winfo rgb . systemLabelColor returns a weird result on aqua](https://core.tcl-lang.org/tk/tktview/23b57a)
 - [Aqua: background thread became slower](https://core.tcl-lang.org/tk/tktview/547cc6)
 - [Use of Tcl_Obj vs char * in Widget storage](https://core.tcl-lang.org/tk/tktview/f91aa2)
 - [cannot build .chm help file (Windows)](https://core.tcl-lang.org/tk/tktview/bb110c)
 - [Tk initialization overwrites thread specific data](https://core.tcl-lang.org/tk/tktview/bcbf4c)
 - [File clamTheme.tcl misses code related to the -indicatorforeground option](https://core.tcl-lang.org/tk/tktview/a69fd7)
 - [Segfault when using menu(button) with the -font option](https://core.tcl-lang.org/tk/tktview/8ce672)
 - [Bind mechanism vs. GNOME](https://core.tcl-lang.org/tk/tktview/6bdf1a)
 - [many PIXEL options don't keep their configured value](https://core.tcl-lang.org/tk/tktview/29ba53)
 - [Menu entry underline does not consider activeborderwidth](https://core.tcl-lang.org/tk/tktview/844c0b)

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
 - [Inconsistent reporting of child geometry changes to grid container](https://core.tcl-lang.org/tk/tktview/beaa8e)
 - [Inconsistency in whether widgets allow negative borderwidths](https://core.tcl-lang.org/tk/tktview/5f739d)
 - [slow widget creation if default font is not used](https://core.tcl-lang.org/tk/tktview/8da7af)
 - [The wm manage command does not work on current macOS versions](https://core.tcl-lang.org/tk/tktview/8a6012)
 - [Slow processing irregular transparencies](https://core.tcl-lang.org/tk/tktview/919066)
 - [text's cursor width on 0th column](https://core.tcl-lang.org/tk/tktview/47fbfc)
 - [text widget breaks graphemes with combining diacritical marks](https://core.tcl-lang.org/tk/tktview/442208)

