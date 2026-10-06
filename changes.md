
The source code for Tk is managed by fossil.  Tk developers coordinate all
changes to the Tk source code at

> [Tk Source Code](https://core.tcl-lang.org/tk/)

Release Tk 9.1.1 arises from the check-in with tag `core-9-1-1`.

Tk 9.1.1 continues the Tk 9.x series of releases.  The Tk 9.x series
do not support Tcl 8.6.  The Tk 9.1 series extends the Tcl 9.0 series.
To make use of Tk 9.1.1, first a Tcl 9.0 or 9.1 release must be present.
As new Tk features are developed, expect them to appear in Tk 9.2+, but not
in Tk 8.

Tk patch releases have the primary purpose of delivering bug fixes
to the userbase.

# Bug fixes
 - [to allow select when no open/close indicator](https://core.tcl-lang.org/tk/tktview/7f3bea)
 - [MS-Win: tk_getOpenFile and tk_getSaveFile leak memory when -initialfile is given more than once](https://core.tcl-lang.org/tk/tktview/7ca431)
 - [double click is lost if processing a click takes too long](https://core.tcl-lang.org/tk/tktview/195423)
 - [canvas scrolls when the scrollbar indicates that it cannot](https://core.tcl-lang.org/tk/tktview/214852)
 - [Make --disable-bidi compile/work again.](https://core.tcl-lang.org/tk/tktview/0ec933)
 - [configure: X11 header check always fails](https://core.tcl-lang.org/tk/tktview/9087fa)
 - [Text widget: wrong bbox widths for non-ASCII characters with bidi text rendering](https://core.tcl-lang.org/tk/tktview/7579c3)
 - [creating widgets is slow when an input method is active](https://core.tcl-lang.org/tk/tktview/5d52ad)
 - [menu entries are drawn directly on the screen](https://core.tcl-lang.org/tk/tktview/791527)
 - [slow widget creation if default font is not used](https://core.tcl-lang.org/tk/tktview/8da7af)
 - [window is not focused on create](https://core.tcl-lang.org/tk/tktview/2effa4)
 - [send to a dead application returned "target application died"](https://core.tcl-lang.org/tk/tktview/729f9c)
 - [configure may not find cups.h](https://core.tcl-lang.org/tk/tktview/3ac12f)
 - [MS-Win: fix grab-global window inresponsive after key Win-D](https://core.tcl-lang.org/tk/tktview/3138512)
 - [X11 <KeyRelease> should not fire on repeated key](https://core.tcl-lang.org/tk/tktview/d3b964)
 - [loop race in canvas Enter / Leave bindings](https://core.tcl-lang.org/tk/tktview/181359)
 - [After "wm withdraw .;wm state . iconic", "wm state ." always returns "withdrawn"](https://core.tcl-lang.org/tk/tktview/d01ec1)
 - [ttk -justify widget option is ignored](https://core.tcl-lang.org/tk/tktview/e6b5b7)
 - [ttk::spinbox -font does not resize buttons](https://core.tcl-lang.org/tk/tktview/330155)
 - [Button should stay in -overrelief state after event](https://core.tcl-lang.org/tk/tktview/110051)
 - [MS-Win: tk_getOpenFile errors out if no toplevel has been mapped yet](https://core.tcl-lang.org/tk/tktview/35a8df)
 - [MS-Win: photo -gamma and -palette are ignored](https://core.tcl-lang.org/tk/tktview/32f2e1)
 - [Displaced menus do not stay open](https://core.tcl-lang.org/tk/tktview/470331)
 - [Menu items accidentally selected](https://core.tcl-lang.org/tk/tktview/680660)
 - [Menu behaviour at screen bottom](https://core.tcl-lang.org/tk/tktview/159666)
 - [Linux menu problem if menu near the bottom of screen](https://core.tcl-lang.org/tk/tktview/f9d316)
 - [X11: menus taller than the screen cannot be scrolled](https://core.tcl-lang.org/tk/tktview/4e7fbe)
 - [MS-Win: last cluster of a drawn range loses its trailing glyphs (combining marks)](https://core.tcl-lang.org/tk/tktview/d41b4d)
 - ["$canvas postscript" crashes on X11 with never mapped windows](https://core.tcl-lang.org/tk/tktview/f7d4e4)
 - [ttk::combobox: popdown list goes off screen when it fits neither below nor above](https://core.tcl-lang.org/tk/tktview/001f8d)
 - [Tk text line breaks Thai words](https://core.tcl-lang.org/tk/info/b7ded5)
 - [possible code issue: return before END_DRAWING](https://core.tcl-lang.org/tk/tktview/1c9965)
 - [ttk::spinbox doesn't expand its height on Linux](https://core.tcl-lang.org/tk/tktview/331303)
 - [spinbox -values overrides the value of the -textvariable](https://core.tcl-lang.org/tk/tktview/143926)
 - [Improper look of ttk::checkbutton -style Toolbutton](https://core.tcl-lang.org/tk/tktview/341467)
 - [ttk Toolbutton has wrong default anchor](https://core.tcl-lang.org/tk/tktview/186783)
 - [segfault in textDisp-26.14.2](https://core.tcl-lang.org/tk/tktview/7353d9)
 - ["BadAlloc (insufficient resources for operation)" in the grid command](https://core.tcl-lang.org/tk/tktview/bffa79)
 - [X11: wm deiconify loses the position of a window moved by the user](https://core.tcl-lang.org/tk/tktview/4c5184)
 - [reject unknown -format options in the GIF, PNG and PPM photo image handlers](https://core.tcl-lang.org/tk/tktview/fef61f)
 - [console doesn't handle <<PasteSelection>>](https://core.tcl-lang.org/tk/tktview/329543)
 - [macOS: Pressing <Tab> in wish console for command completion loses keyboard input](https://core.tcl-lang.org/tk/tktview/9474bd)
 - [Focused button widgets invoked on Alt-Space](https://core.tcl-lang.org/tk/tktview/169275)
 - ["focus" dumps core with certain extensions](https://core.tcl-lang.org/tk/tktview/704212)
 - [canvas,text,ttk widgets: call scrollbar commands after window mapping (and not with 1x1 initial size)](https://core.tcl-lang.org/tk/tktview/991849)
 - [Cascade in a multi-column menu is posted at the right edge of the whole menu instead of its column](https://core.tcl-lang.org/tk/tktview/fcb139)
 - [MinGW: compilation using installed Tcl fails if Tcl source is not accessible](https://core.tcl-lang.org/tk/tktview/b900dc)
 - [text widget's see subcommand can take many seconds](https://core.tcl-lang.org/tk/tktview/80213d)
 - [Aqua: "pack forget" leaves window on screen](https://core.tcl-lang.org/tk/tktview/2ef5dd)
 - [leak of a new value object of a custom option](https://core.tcl-lang.org/tk/tktview/2b6398)
 - [Make the base chunk of the text layout thread-specific](https://core.tcl-lang.org/tk/tktview/a0c1a9)
 - [wrapped text counts spaces at end of line, splits words, breaks after leading spaces (X11 with bidi, Windows)](https://core.tcl-lang.org/tk/tktview/d08538)

Release Tk 9.1.0 arises from the check-in with tag `core-9-1-0`.

Tk 9.1.0 continues the Tk 9.x series of releases.  The Tk 9.x series
do not support Tcl 8.6.  The Tk 9.1 series extends the Tcl 9.0 series.
To make use of Tk 9.1.0, first a Tcl 9.0 or 9.1 release must be present.
As new Tk features are developed, expect them to appear in Tk 9.2+, but not
in Tk 8.

# Potential incompatibilities to 9.0
 - [MS-Win: Remove the -xpstyle option from tk_chooseDirectory and tk_getOpenFile](https://core.tcl-lang.org/tk/tktview/441c52)
 - [MS-Win: Eliminate the "xpnative" ttk style, in favor of "vista"](https://core.tcl-lang.org/tk/tktview/441c52)
 - [No longer allow negative screen distances in most cases](https://core.tcl-lang.org/tips/doc/trunk/tip/698.md)
 - [BiDi support is now the default build option on X11 and harfbuzz is a hard build dependency](https://core.tcl-lang.org/tk/tktview/1b81ff43a2)

# 9.1 Features and Interfaces
 - [MS-Win: remove Windows XP dialog variants for tk_chooseDirectory and tk_getOpenFile](https://core.tcl-lang.org/tk/tktview/441c52)
 - [Extend Tk_CanvasTextInfo](https://core.tcl-lang.org/tips/doc/trunk/tip/704.md)
 - [Add new states to ttk::treeview and ttk::notebook](https://core.tcl-lang.org/tips/doc/trunk/tip/719.md)
 - [Limit tk_messageBox to physical screen width](https://core.tcl-lang.org/tk/info/e19f1d891)
 - [Constrain own Dialogs to the physical screen size](https://core.tcl-lang.org/tk/info/7c28f835)
 - [Add a ttk::toggleswitch widget to the core](https://core.tcl-lang.org/tips/doc/trunk/tip/727.md)
 - [Add a tk attribtable command to the core](https://core.tcl-lang.org/tips/doc/trunk/tip/729.md)
 - [Implement more X11 region functions on Windows and Aqua](https://core.tcl-lang.org/tk/info/50fdbc36ad)
 - [Add accessibility/screen reader support to the core](https://core.tcl-lang.org/tips/doc/trunk/tip/733.md)
 - [Scroll entry with mouse wheel](https://core.tcl-lang.org/tips/doc/trunk/tip/736.md)
 - [Add a Wide.TSpinbox style to the core](https://core.tcl-lang.org/tips/doc/trunk/tip/739.md)
 - [Add support for native file icons to the core](https://core.tcl-lang.org/tips/doc/trunk/tip/743.md)
 - [Re-implement the Aqua send command](https://core.tcl-lang.org/tk/info/1574913cc772201e)
 - [Make the selection colors of the listbox widget fully native-conform](https://core.tcl-lang.org/tips/doc/trunk/tip/747.md)
 - [Add support for bidrectional text / RTL languages on Windows and X11](https://core.tcl-lang.org/tk/tktview/1b81ff43a2)
 - [Add Mouse Wheel Zoom Support to Tk Console](https://core.tcl-lang.org/tips/doc/trunk/tip/742.md)
 - [Consistent dark mode for Windows and macOS](https://core.tcl-lang.org/tips/doc/trunk/tip/750.md)
 - [Rotated text for label widgets](https://core.tcl-lang.org/tips/doc/trunk/tip/751.md)
 - [ttk::treeview enhancements](https://core.tcl-lang.org/tips/doc/trunk/tip/740.md)
 - [Painting performance improvements for images in ttk widgets](https://core.tcl-lang.org/tk/info/7caf9e9edcfbee42)
 - [Locale support for word handling in text and entry](https://core.tcl-lang.org/tips/doc/trunk/tip/687.md)
 - [Provide access to the full contents of the Info.plist file of a Tk-based macOS app](https://core.tcl-lang.org/tips/doc/trunk/tip/725.md)

