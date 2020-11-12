# systray.tcl --

# This file defines the systray command for icon display and manipulation
# in the system tray on X11, Windows, and macOS, and the ::tk::sysnotify command
# for system alerts on each platform. It implements an abstraction layer that
# presents a consistent API across the three platforms.

# Copyright © 2020 Kevin Walzer/WordTech Communications LLC.
#
# See the file "license.terms" for information on usage and redistribution
# of this file, and for a DISCLAIMER OF ALL WARRANTIES.

# Pure-Tcl system tooltip window for use with system tray icon if native
# implementation not available.

namespace eval ::tk::systray:: {

    variable ::tk::systray::_iconlist
    set ::tk::systray::_iconlist {}
    
    proc _balloon {w help} {
	bind $w <Any-Enter> "after 1000 [list ::tk::systray::_balloon_show %W [list $help]]"
	bind $w <Any-Leave> "destroy %W._balloon"
    }

    proc _balloon_show {w arg} {
	if {[eval winfo containing  [winfo pointerxy .]]!=$w} {return}
	set top $w._balloon
	catch {destroy $top}
	toplevel $top -bg black
	wm overrideredirect $top 1
	if {[tk windowingsystem] eq "aqua"}  {
	    ::tk::unsupported::MacWindowStyle style $top help none
	}
	pack [message $top._txt -aspect 10000  \
		  -text $arg]
	set wmx [winfo rootx $w]
	set wmy [expr {[winfo rooty $w] + [winfo height $w]}]
	wm geometry $top [winfo reqwidth $top._txt]x[winfo reqheight $top._txt]+$wmx+$wmy
	raise $top
    }

    proc _win_callback {msg icn} {

	switch -exact -- $msg {
	    WM_LBUTTONDOWN {
		eval $::winicoprops::cb1
	    }
	    WM_RBUTTONDOWN {
		eval $::winicoprops::cb3
	    }
	}
    }
    namespace export *
}


# Additional infrastructure for Windows variables and callbacks.

namespace eval ::winicoprops {
    variable ico
    variable cb1
    variable cb3
    set ico ""
    set cb1 ""
    set cb3 ""
}

# Pure-Tcl system notification window for use if native implementation not available.
namespace eval ::tk::sysnotify:: {

    proc _notifywindow {title msg} {
	catch {destroy ._notify}
	set w [toplevel ._notify]
	if {[tk windowingsystem] eq "aqua"} {
	    ::tk::unsupported::MacWindowStyle style $w utility {hud
		closeBox resizable}
	    wm title $w "Alert"
	}
	if {[tk windowingsystem] eq "win32"} {
	    wm attributes $w -toolwindow true
	    wm title $w "Alert"
	}
	label $w.l -bg gray30 -fg white -image ::tk::icons::information
	pack $w.l -fill both -expand yes -side left
	message $w.message -aspect 150 -bg gray30 -fg white -aspect 150 -text $title\n\n$msg -width 280
	pack $w.message -side right -fill both -expand yes
	if {[tk windowingsystem] eq "x11"} {
	    wm overrideredirect $w true
	}
	wm attributes $w -alpha 0.0
	set xpos [expr {[winfo screenwidth $w] - 325}]
	wm geometry $w +$xpos+30
	::tk::sysnotify::_fade_in $w
	after 3000 ::tk::sysnotify::_fade_out $w
    }

    #Fade and destroy window.
    proc _fade_out {w} {
	catch {
	    set prev_degree [wm attributes $w -alpha]
	    set new_degree [expr {$prev_degree - 0.05}]
	    set current_degree [wm attributes $w -alpha $new_degree]
	    if {$new_degree > 0.0 && $new_degree != $prev_degree} {
		after 10 [list ::tk::sysnotify::_fade_out $w]
	    } else {
		destroy $w
	    }
	}
    }

    #Fade the window into view.
    proc _fade_in {w} {
	catch {
	    raise $w
	    wm attributes $w -topmost 1
	    set prev_degree [wm attributes $w -alpha]
	    set new_degree [expr {$prev_degree + 0.05}]
	    set current_degree [wm attributes $w -alpha $new_degree]
	    focus -force $w
	    if {$new_degree < 0.9 && $new_degree != $prev_degree} {
		after 10 [list ::tk::sysnotify::_fade_in $w]
	    }
	}
    }
    namespace export *
}



# systray --
# This procedure creates an icon display in the platform-specific system tray.
#
# Subcommands:
#
#     create - create systray icon.
#         Arguments:
#             image - Tk image to display.
#             text - string to display in tooltip over image.
#             b1_callback - Tcl proc to invoke on <Button-1> event.
#             b3_callback - Tcl proc to invoke on <Button-3> event.

#     modify - change one of the systray properties.
#         Arguments (only one required):
#             image - Tk image to update.
#             text - string to update.
#             b1_callback - Tcl proc to change.
#             b3_callback - Tcl proc to change.
#     destroy - destroy systray icon.
#         Arguments:
#             none.
proc ::tk::systray {args} {

    if {[llength $args] == 0} {
	error "wrong # args: should be \"tk systray create | modify | destroy\""
    }

    set name [lindex $args 0]
    if {![string equal $name "create"]  && ![string equal $name "modify"]  && ![string equal $name "destroy"]} {
	error "bad option \"$name\": must be create, modify, or destroy"
    }


    #Remove the systray icon.
    if {[lindex $args 0] eq "destroy" && [llength $args] == 1} {
	switch -- [tk windowingsystem] {
	    "win32" {
		set ::tk::systray::_iconlist {}
		_systray taskbar delete $::winicoprops::ico
		set ::winicoprops::ico ""
	    }
	    "x11" {
		destroy ._tray
	    }
	    "aqua" {
		_systray destroy
	    }
	}
    }

    if {[lindex $args 0] eq "destroy" && [llength $args] > 1} {
	error "wrong # args: should be \"tk systray destroy\""
    }

    #Create the system tray icon.
    if {[lindex $args 0] eq "create"} {
	if {[llength $args] != 5} {
	    error "wrong # args: should be \"tk systray create image text b1_callback b3_callback\""
	}
	set ::winicoprops::cb1 [lindex $args 3]
	set ::winicoprops::cb3 [lindex $args 4]
	switch -- [tk windowingsystem] {
	    "win32" {
		if {[llength $::tk::systray::_iconlist] > 0} {
		    error "Only one system tray icon supported per interpeter"
		}
		set ::winicoprops::ico [_systray createfrom [lindex $args 1]]
		_systray taskbar add $::winicoprops::ico -text [lindex $args 2] -callback [list ::tk::systray::_win_callback %m %i]
		lappend ::tk::systray::__iconlist "ico#[llength ::tk::systray::_iconlist]"
	    }
	    "x11" {
		if [winfo exists ._tray] {
		    error  "Only one system tray icon supported per interpeter"
		}
		_systray ._tray -image $::winicoprops::img -visible true
		::tk::systray::_balloon ._tray $::winicoprops::txt
		bind ._tray <Button-1> $::winicoprops::cb1
		bind ._tray <Button-3> $::winicoprops::cb3
	    }
	    "aqua" {
		_systray create [lindex $args 1] [lindex $args 2] $::winicoprops::cb1 $::winicoprops::cb3
	    }
	}
    }

    #Modify the system tray icon properties.
    if {[lindex $args 0] eq "modify"} {
	if {[llength $args] != 3} {
	    error "wrong # args: should be \"tk systray modify image | text | b1_callback | b3_callback option\""
	}
	switch -- [tk windowingsystem] {
	    "win32" {
		switch -- [lindex $args 1] {
		    image {
		        set txt [_systray text $::winicoprops::ico]
		        _systray taskbar delete $::winicoprops::ico
		        set ::winicoprops::ico [_systray createfrom [lindex $args 2]]
		        _systray taskbar add $::winicoprops::ico -text $txt -callback [list ::tk::systray::_win_callback %m %i]
		    }
		    text {
		        _systray taskbar modify $::winicoprops::ico -text [lindex $args 2]
		    }
		    b1_callback {
		        set ::winicoprops::cb1 [lindex $args 2]
		        _systray taskbar modify $::winicoprops::ico -callback [list ::tk::systray::_win_callback %m %i]
		    }
		    b3_callback {
		        set ::winicoprops::cb3 [lindex $args 2]
		        _systray taskbar modify $::winicoprops::ico -callback [list ::tk::systray::_win_callback %m %i]
		    }
		    default {
		        error "unknown option \"[lindex $args 1]\": must be image, text, b1_callback, or b3_callback"
		    }
		}
	    }
	    "x11" {
		switch -- [lindex $args 1] {
		    image {
		        ._tray configure -image [lindex $args 2]
		    }
		    text {
			::tk::systray::_balloon ._tray [lindex $args 2]
		    }
		    b1_callback {
		        bind ._tray <Button-1> [lindex $args 2]
		    }
		    b3_callback {
		        bind ._tray <Button-3> [lindex $args 2]
		    }
		    default {
		        error "unknown option \"[lindex $args 1]\": must be image, text, b1_callback, or b3_callback"
		    }
		}
	    }
	    "aqua" {
		switch -- [lindex $args 1] {
		    image       -
		    text        -
		    b1_callback -
		    b3_callback {
		        _systray modify [lindex $args 1] [lindex $args 2]
		    }
		    default {
		        error "unknown option \"[lindex $args 1]\": must be image, text, b1_callback, or b3_callback"
		    }
		}
	    }
	}
    }
    if {[tk windowingsystem] eq "win32"} {
	if {$::winicoprops::ico ne ""} {
	    bind . <Destroy> {catch {_systray taskbar delete $::winicoprops::ico ; set ::winicoprops::ico ""}}
	}
    }
}

# sysnotify --
# This procedure implements a platform-specific system notification alert.
#
#   Arguments:
#       title - main text of alert.
#       message - body text of alert.

proc ::tk::sysnotify {title message} {

    switch -- [tk windowingsystem] {
	"win32" {
	    if {$::winicoprops::ico eq ""} {
		error "Must create a system tray icon with the \"tk systray\" command first"
	    }
	    _sysnotify notify $::winicoprops::ico $title $message
	}
	"x11" {
	    if {[info commands _sysnotify] eq ""} {
		::tk::sysnotify::_notifywindow $title $message
	    } else {
		_sysnotify $title $message
	    }
	}
	"aqua" {
	    _sysnotify $title $message
	}
    }
    return
}

#Add these commands to the tk command ensemble: tk systray, tk sysnotify
#Thanks to Christian Gollwitzer for the guidance here
set map [namespace ensemble configure tk -map]
dict set map systray ::tk::systray
dict set map sysnotify ::tk::sysnotify
namespace ensemble configure tk -map $map
