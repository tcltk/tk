
The source code for Tk is managed by fossil.  Tk developers coordinate all
changes to the Tk source code at

> [Tk Source Code](https://core.tcl-lang.org/tk/)

Release Tk 9.1a0 arises from the check-in with tag `core-9-1-a0`.

Tk 9.1a0 continues the Tk 9.x series of releases.  The Tk 9.x series
do not support Tcl 8.6.  The Tk 9.1 series extends the Tcl 9.0 series.
To make use of Tk 9.1a0, first a Tcl 9.0 or 9.1 release must be present.
As new Tk features are developed, expect them to appear in Tk 9, but not
necessarily in Tk 8.

# 9.1 Features and Interfaces
 - [MS-Win: remove Windows XP dialog variants for tk_chooseDirectory and tk_getOpenFile](https://core.tcl-lang.org/tk/tktview/441c52)
 - [Handle negative screen distances](https://core.tcl-lang.org/tips/doc/trunk/tip/698.md)
 - [Extend Tk_CanvasTextInfo](https://core.tcl-lang.org/tips/doc/trunk/tip/704.md)
 - [Add new states to ttk::treeview and ttk::notebook](https://core.tcl-lang.org/tips/doc/trunk/tip/719.md)

# Potential incompatibilities to 9.0
 - [MS-Win: the undocumented option -xpstyle was removed from tk_chooseDirectory and tk_getOpenFile](https://core.tcl-lang.org/tk/tktview/441c52)
