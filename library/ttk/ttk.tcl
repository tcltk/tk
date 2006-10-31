#
# $Id: ttk.tcl,v 1.1 2006/10/31 01:42:27 hobbs Exp $
#
# Ttk widget set initialization script.
#

### Source library scripts.
#

namespace eval ::ttk {
    variable library
    if {![info exists library]} {
	set library [file dirname [info script]]
    }
}

source [file join $::ttk::library keynav.tcl]
source [file join $::ttk::library fonts.tcl]
source [file join $::ttk::library cursors.tcl]
source [file join $::ttk::library icons.tcl]
source [file join $::ttk::library utils.tcl]

## ttk::deprecated $old $new --
#	Define $old command as a deprecated alias for $new command
#	$old and $new must be fully namespace-qualified.
#
proc ::ttk::deprecated {old new} {
    interp alias {} $old {} ttk::do'deprecate $old $new
}
## do'deprecate --
#	Implementation procedure for deprecated commands --
#	issue a warning (once), then re-alias old to new.
#
proc ::ttk::do'deprecate {old new args} {
    deprecated'warning $old $new
    interp alias {} $old {} $new
    eval [linsert $args 0 $new]
}

## deprecated'warning --
#	Gripe about use of deprecated commands.
#
proc ::ttk::deprecated'warning {old new} {
    puts stderr "$old deprecated -- use $new instead"
}

### Forward-compatibility.
#
# ttk::panedwindow used to be named ttk::paned.  Keep the alias for now.
#
::ttk::deprecated ::ttk::paned ::ttk::panedwindow

if {[info exists ::ttk::deprecrated] && $::ttk::deprecated} {
    ### Deprecated bits.
    #

    namespace eval ::tile {
	# Deprecated namespace.  Define these only when requested
	variable library
	if {![info exists library]} {
	    set library [file dirname [info script]]
	}

	variable version 0.7.8
	variable patchlevel 0.7.8
    }
    package provide tile $::tile::version

    ### Widgets.
    #	Widgets are all defined in the ::ttk namespace.
    #
    #	For compatibility with earlier Tile releases, we temporarily
    #	create aliases ::tile::widget, and ::t$widget.
    #	Using any of the aliases will issue a warning.
    #

    namespace eval ttk {
	variable widgets {
	    button checkbutton radiobutton menubutton label entry
	    frame labelframe scrollbar
	    notebook progressbar combobox separator
	    scale
	}

	variable wc
	foreach wc $widgets {
	    namespace export $wc

	    deprecated ::t$wc ::ttk::$wc
	    deprecated ::tile::$wc ::ttk::$wc
	    namespace eval ::tile [list namespace export $wc]
	}
    }
}

### ::ttk::ThemeChanged --
#	Called from [::ttk::style theme use].
#	Sends a <<ThemeChanged>> virtual event to all widgets.
#
proc ::ttk::ThemeChanged {} {
    set Q .
    while {[llength $Q]} {
	set QN [list]
	foreach w $Q {
	    event generate $w <<ThemeChanged>>
	    foreach child [winfo children $w] {
		lappend QN $child
	    }
	}
	set Q $QN
    }
}

### Public API.
#

proc ::ttk::themes {{ptn *}} {
    set themes [list]

    foreach pkg [lsearch -inline -all -glob [package names] ttk::theme::$ptn] {
	lappend themes [namespace tail $pkg]
    }

    return $themes
}

## ttk::setTheme $theme --
#	Set the current theme to $theme, loading it if necessary.
#
proc ::ttk::setTheme {theme} {
    variable currentTheme ;# @@@ Temp -- [::ttk::style theme use] doesn't work
    if {$theme ni [::ttk::style theme names]} {
	package require ttk::theme::$theme
    }
    ::ttk::style theme use $theme
    set currentTheme $theme
}

### Load widget bindings.
#
source [file join $::ttk::library button.tcl]
source [file join $::ttk::library menubutton.tcl]
source [file join $::ttk::library scrollbar.tcl]
source [file join $::ttk::library scale.tcl]
source [file join $::ttk::library progress.tcl]
source [file join $::ttk::library notebook.tcl]
source [file join $::ttk::library panedwindow.tcl]
source [file join $::ttk::library entry.tcl]
source [file join $::ttk::library combobox.tcl]	;# dependency: entry.tcl
source [file join $::ttk::library treeview.tcl]
source [file join $::ttk::library sizegrip.tcl]
source [file join $::ttk::library dialog.tcl]

## Label and Labelframe bindings:
#  (not enough to justify their own file...)
#
bind TLabelframe <<Invoke>>	{ tk::TabToWindow [tk_focusNext %W] }
bind TLabel <<Invoke>>		{ tk::TabToWindow [tk_focusNext %W] }

### Load themes.
#
source [file join $::ttk::library defaults.tcl]
source [file join $::ttk::library classicTheme.tcl]
source [file join $::ttk::library altTheme.tcl]
source [file join $::ttk::library clamTheme.tcl]

### Choose platform-specific default theme.
#
# Notes:
#	+ xpnative takes precedence over winnative if available.
#	+ On X11, users can use the X resource database to
#	  specify a preferred theme (*TkTheme: themeName)
#

set ::ttk::defaultTheme "default"

if {[package provide ttk::theme::winnative] != {}} {
    source [file join $::ttk::library winTheme.tcl]
    set ::ttk::defaultTheme "winnative"
}
if {[package provide ttk::theme::xpnative] != {}} {
    source [file join $::ttk::library xpTheme.tcl]
    set ::ttk::defaultTheme "xpnative"
}
if {[package provide ttk::theme::aqua] != {}} {
    source [file join $::ttk::library aquaTheme.tcl]
    set ::ttk::defaultTheme "aqua"
}

set ::ttk::userTheme [option get . tkTheme TkTheme]
if {$::ttk::userTheme != {}} {
    if {($::ttk::userTheme in [::ttk::style theme names])
        || ![catch {package require ttk::theme::$ttk::userTheme}]} {
	set ::ttk::defaultTheme $::ttk::userTheme
    }
}

::ttk::setTheme $::ttk::defaultTheme

#*EOF*
