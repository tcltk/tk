# systray.tcl --

# This file defines the 'tk systray' command for icon display and manipulation
# in the system tray on X11, Windows, and macOS, and the 'tk sysnotify' command
# for system alerts on each platform. It implements an abstraction layer that
# presents a consistent API across the three platforms.

# Copyright © 2020 Kevin Walzer/WordTech Communications LLC.
# Copyright © 2020 Eric Boudaillier.
# Copyright © 2020 Francois Vogel.
#
# See the file "license.terms" for information on usage and redistribution
# of this file, and for a DISCLAIMER OF ALL WARRANTIES.

# Pure-Tcl system tooltip window for use with system tray icon if native
# implementation not available.

namespace eval ::tk::systray {
    variable _created 0
    variable _options {-image "" -text "" -button1 "" -button3 ""}
    variable _current {}
    variable _ico

    proc _balloon {w help} {
	bind $w <Any-Enter> "after 1000 [list ::tk::systray::_balloon_show %W [list $help]]"
	bind $w <Any-Leave> "destroy %W._balloon"
    }

    proc _balloon_show {w arg} {
	if {[winfo containing {*}[winfo pointerxy .]] ne $w} {
	    return
	}
	set top $w._balloon
	catch {destroy $top}
	toplevel $top -bg black
	wm overrideredirect $top 1
	if {[tk windowingsystem] eq "aqua"}  {
	    ::tk::unsupported::MacWindowStyle style $top help none
	}
	pack [message $top._txt -aspect 10000 -text $arg]
	set wmx [winfo rootx $w]
	set wmy [expr {[winfo rooty $w] + [winfo height $w]}]
	wm geometry $top [winfo reqwidth $top._txt]x[winfo reqheight $top._txt]+$wmx+$wmy
	raise $top
    }

    proc _win_callback {msg} {
	variable _current
	# The API at the Tk level does not feature bindings to double clicks. Whatever
	# the speed the user clicks with, he expects the single click binding to fire.
	# Therefore we need to bind to both WM_*BUTTONDOWN and to WM_*BUTTONDBLCLK.
	switch -exact -- $msg {
	    WM_LBUTTONDOWN - WM_LBUTTONDBLCLK {
		uplevel #0 [dict get $_current -button1]
	    }
	    WM_RBUTTONDOWN - WM_RBUTTONDBLCLK {
		uplevel #0 [dict get $_current -button3]
	    }
	}
    }

    namespace export create configure destroy
    namespace ensemble create
}


# Pure-Tcl system notification window for use if native implementation not available.
namespace eval ::tk::sysnotify:: {

    proc _notifywindow {title msg} {
	catch {destroy ._notify}
	set w [toplevel ._notify]
	if {[tk windowingsystem] eq "aqua"} {
	    ::tk::unsupported::MacWindowStyle style $w utility {hud closeBox resizable}
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


# tk systray --
# This procedure creates an icon display in the platform-specific system tray.
#
# Subcommands:
#
#     create - create systray icon.
#         Arguments:
#             -image - Tk image to display.
#             -text - string to display in tooltip over image.
#             -button1 - Tcl proc to invoke on <Button-1> event.
#             -button3 - Tcl proc to invoke on <Button-3> event.
#
#     configure - change one of the systray properties.
#         Arguments (Any or all can be called):
#             -image - Tk image to update.
#             -text - string to update.
#             -button1 - Tcl proc to change for <Button-1> event.
#             -button3  - Tcl proc to change for <Button-3> event.
#
#     destroy - destroy systray icon.
#         Arguments:
#             none.
proc ::tk::systray::create {args} {
    variable _created
    variable _options
    variable _current
    variable _ico

    if {$_created} {
	return -code error -errorcode {TK SYSTRAY CREATE} "only one system tray icon supported per interpeter"
    }
    _check_options $args 0
    if {![dict exists $args -image]} {
	return -code error -errorcode {TK SYSTRAY CREATE} "missing required option \"-image\""
    }
    set values [dict merge $_options $args]
    try {
	switch -- [tk windowingsystem] {
	    "win32" {
		set _ico [_systray add -image [dict get $values -image] \
			-text [dict get $values -text] \
			-callback [list ::tk::systray::_win_callback %m]]
	    }
	    "x11" {
		_systray ._tray -image [dict get $values -image] -visible true
		_balloon ._tray [dict get $values -text]
		bind ._tray <Button-1> [dict get $values -button1]
		bind ._tray <Button-3> [dict get $values -button3]
	    }
	    "aqua" {
		_systray create [dict get $values -image] [dict get $values -text] \
			[dict get $values -button1] [dict get $values -button3]
	    }
	}
    } on ok {} {
	set _current $values
	set _created 1
	return
    } on error {msg opts} {
	return -code error -errorcode [dict get $opts -errorcode] $msg
    }
}

# Modify the systray icon.
proc ::tk::systray::configure {args} {
    variable _created
    variable _options
    variable _current
    variable _ico

    if {!$_created} {
	return -code error -errorcode {TK SYSTRAY CREATE} "systray not created"
    }
    _check_options $args 1
    if {[llength $args] == 0} {
	return $_current
    } elseif {[llength $args] == 1} {
	return [dict get $_current [lindex $args 0]]
    }
    set values [dict merge $_current $args]
    try {
	switch -- [tk windowingsystem] {
	    "win32" {
		if {[dict exists $args -image]} {
		    _systray modify $_ico -image [dict get $args -image]
		}
		if {[dict exists $args -text]} {
		    _systray modify $_ico -text [dict get $args -text]
		}
	    }
	    "x11" {
		if {[dict exists $args -image]} {
		    ._tray configure -image [dict get $args -image]
		}
		if {[dict exists $args -text]} {
		    _balloon ._tray [dict get $args -text]
		}
		if {[dict exists $args -button1]} {
		    bind ._tray <Button-1> [dict get $args -button1]
		}
		if {[dict exists $args -button3]} {
		    bind ._tray <Button-3> [dict get $args -button3]
		}
	    }
	    "aqua" {
		foreach {key opt} {image -image text \
			-text b1_callback -button1 b3_callback -button3} {
		    if {[dict exists $args $opt]} {
			_systray modify $key [dict get $args $opt]
		    }
		}
	    }
	}
    } on ok {} {
	set _current $values
	return
    } on error {msg opts} {
	return -code error -errorcode [dict get $opts -errorcode] $msg
    }
}


# Remove the systray icon.
proc ::tk::systray::destroy {} {
    variable _created
    variable _current
    variable _ico

    if {!$_created} {
	return -code error -errorcode {TK SYSTRAY DESTROY} "systray not created"
    }
    switch -- [tk windowingsystem] {
	"win32" {
	    _systray delete $_ico
	    set _ico ""
	}
	"x11" {
	    ::destroy ._tray
	}
	"aqua" {
	    _systray destroy
	}
    }
    set _created 0
    set _current {}
    return
}

# Check systray options
proc ::tk::systray::_check_options {argsList singleOk} {
    variable _options

    set len [llength $argsList]
    while {[llength $argsList] > 0} {
        set opt [lindex $argsList 0]
        if {![dict exists $_options $opt]} {
            tailcall return -code error -errorcode {TK SYSTRAY OPTION} \
		"unknown option \"$opt\": must be -image, -text, -button1 or -button3"
        }
        if {[llength $argsList] == 1 && !($len == 1 && $singleOk)} {
            tailcall return -code error -errorcode {TK SYSTRAY OPTION} \
		"missing value for option \"$opt\""
        }
        set argsList [lrange $argsList 2 end]
    }
}

# tk sysnotify --
# This procedure implements a platform-specific system notification alert.
#
#   Arguments:
#       title - main text of alert.
#       message - body text of alert.

proc ::tk::sysnotify::sysnotify {title message} {

    switch -- [tk windowingsystem] {
	"win32" {
	    if {!$::tk::systray::_created} {
		error "must create a system tray icon with the \"tk systray\" command first"
	    }
	    _sysnotify notify $::tk::systray::_ico $title $message
	}
	"x11" {
	    if {[info commands ::tk::sysnotify::_sysnotify] eq ""} {
		_notifywindow $title $message
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
namespace ensemble configure tk -map \
    [dict merge [namespace ensemble configure tk -map] \
        {systray ::tk::systray sysnotify ::tk::sysnotify::sysnotify}]
