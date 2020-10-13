# systray.tcl --

# This file defines the systray command for icon display and manipulation
# in the system tray on X11, Windows, and macOS, and the ::tk::systnotify command
# for system alerts on each platform. It implements an abstraction layer that
# presents a consistent API across the three platforms.

# Copyright (c) 2020 Kevin Walzer/WordTech Communications LLC.
#
# See the file "license.terms" for information on usage and redistribution
# of this file, and for a DISCLAIMER OF ALL WARRANTIES.

# Pure-Tcl system tooltip window for use with system tray icon if native implementation not available.

proc _balloon {w help} {
    bind $w <Any-Enter> "after 1000 [list _balloon_show %W [list $help]]"
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
    pack [message $top.txt -aspect 10000  \
	      -text $arg]
    set wmx [winfo rootx $w]
    set wmy [expr {[winfo rooty $w] + [winfo height $w]}]
    wm geometry $top [winfo reqwidth $top.txt]x[
						winfo reqheight $top.txt]+$wmx+$wmy
    raise $top
}

# Additional infrastructure for Windows callbacks.
proc _win_callback {msg icn script} {
    switch -exact -- $msg {
	WM_LBUTTONDOWN {
	    eval $script
	}
    }
}

# Pure-Tcl system notification window for use if native implementation not available.

image create photo _info -data {R0lGODlhIAAgAIQWAEWCtEaCtEeCtEWDtUuFtU6FtlGGtlSIt1KJuHCXvnKYv36ewoGhw52zzp+1z7DB1rHB1rzK3MDN3eTp8Ojs8v7+/////////////////////////////////////////yH+EUNyZWF0ZWQgd2l0aCBHSU1QACH5BAEKAB8ALAAAAAAgACAAAAWO4CeOZGmeaKqubOuOQSzHrznfd/3hvPz2AUGv1YtULJKhqqewOC0J5Qm4eFoYQBQwMHFSANkpsOBoGLbirdpX2hIgjwdEbQMirAN0247Xk7Z3T3lhf3yCfjCGToNSiT2Bi4iOOJAWjDhaj32NhTyVlzmZlJuYS6OHpSs8B6wHPD9rbLBrOp2htbi5ursmIQA7}

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
    label $w.l -bg gray30 -fg white -image _info
    pack $w.l -fill both -expand yes -side left
    message $w.message -aspect 150 -bg gray30 -fg white -aspect 150 -text $title\n\n$msg -width 280
    pack $w.message -side right -fill both -expand yes
    if {[tk windowingsystem] eq "x11"} {
	wm overrideredirect $w true
    }
    wm attributes $w -alpha 0.0
    set xpos [expr {[winfo screenwidth $w] - 325}]
    wm geometry $w +$xpos+30
    _fade_in $w
    after 3000 _fade_out $w
}

#Fade and destroy window.
proc _fade_out {w} {
    catch {
	set prev_degree [wm attributes $w -alpha]
	set new_degree [expr {$prev_degree - 0.05}]
	set current_degree [wm attributes $w -alpha $new_degree]
	if {$new_degree > 0.0 && $new_degree != $prev_degree} {
	    after 10 [list _fade_out $w]
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
	    after 10 [list _fade_in $w]
	}
    }
}


global _ico
set _ico ""

# systray --
# This procedure creates an icon display in the platform-specific system tray.
#
# Subcommands:
#
#     create - create systray icon.
#         Arguments:
#             image - Tk image to display.
#             text - string to display in tooltip over image.
#             callback - Tcl proc to invoke on <Button-1> event.
#     modify - change one of the systray properties.
#         Arguments (only one required):
#             image - Tk image to update.
#             text - string to update.
#             callback - Tcl proc to change.
#     destroy - destroy systray icon.
#         Arguments:
#             none.
proc systray {args} {

    if {[llength $args] == 0} {
	error "wrong # args: should be \"tk systray create | modify | destroy\""
    }

    set name [lindex $args 0]
    if {![string equal $name "create"]  && ![string equal $name "modify"]  && ![string equal $name "destroy"]} {
	error "wrong # args: should be \"tk systray create | modify | destroy\""
    }

    #Set variables for icon properties.
    global _ico
    global img
    global txt
    global cb

    set img ""
    set txt ""
    set cb ""

    #Remove the systray icon.
    if {[lindex $args 0] eq "destroy" && [llength $args] == 1} {
	switch -- [tk windowingsystem] {
	    "win32" {
		_systray taskbar delete $_ico
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
	error "wrong # args: \"tk systray destroy\""
    }

    #Create the system tray icon.
    if {[lindex $args 0] eq "create"} {
	if {[llength $args] != 4} {
	    error "wrong # args: \"tk systray create image? text? callback?\""
	}
        set img [lindex $args 1]
        set txt [lindex $args 2]
        set cb [lindex $args 3]
        switch -- [tk windowingsystem] {
            "win32" {
		set _ico [_systray createfrom $img]
		_systray taskbar add $_ico -text $txt -callback [list _win_callback %m %i $cb]
	    }
            "x11" {
		_systray ._tray -image $img -visible true
		_balloon ._tray $txt
		bind ._tray <Button-1> $cb
	    }
	    "aqua" {
		_systray create $img $txt $cb
	    }
	}
    }
    #Modify the system tray icon properties.
    if {[lindex $args 0] eq "modify"} {
	if {[llength $args] != 3} {
	    error "wrong # args: \"tk systray modify image | text | callback option?\""
	}
	switch -- [tk windowingsystem] {
	    "win32" {
		if {[lindex $args 1] eq "image"} {
		    set img [lindex $args 2]
		    _systray taskbar delete $_ico
		    set _ico [_systray createfrom $img]
		    _systray taskbar add $_ico -text $txt -callback [list _win_callback %m %i $cb]
		}
		if {[lindex $args 1] eq "text"} {
		    set txt [lindex $args 2]
		    _systray taskbar modify $ico -text $txt
		}
		if {[lindex $args 1 ] eq "callback"} {
		    set cb [lindex $args 2]
		    _systray taskbar modify $_ico -callback [list _win_callback %m %i $cb]
		}
	    }
	    "x11" {
		if {[lindex $args 1] eq "image"} {
		    set img [lindex $args 2]
		    ._tray configure -image ""
		    ._tray configure -image $img
		}
		if {[lindex $args 1] eq "text"} {
		    set txt ""
		    set txt [lindex $args 2]
		    _balloon ._tray $txt
		}
		if {[lindex $args 1 ] eq "callback"} {
		    set cb ""
		    bind ._tray <Button-1> ""
		    set cb [lindex $args 2]
		    bind ._tray <Button-1>  $cb
		}
	    }
	    "aqua" {
		if {[lindex $args 1] eq "image"} {
		    set img [lindex $args 2]
		    _systray modify image $img
		}
		if {[lindex $args 1] eq "text"} {
		    set txt [lindex $args 2]
		    _systray modify text $txt
		}
		if {[lindex $args 1 ] eq "callback"} {
		    set cb [lindex $args 2]
		    _systray modify callback $cb
		}
	    }
	}
    }
}

# sysnotify --
# This procedure implements a platform-specific system notification alert.
#
#   Arguments:
#       title - main text of alert.
#       message - body text of alert.

proc sysnotify {title message} {

    global _ico

    switch -- [tk windowingsystem] {
	"win32" {
	    _sysnotify notify $_ico $title $message
	}
	"x11" {
	    if {[info commands _sysnotify] eq ""} {
		_notifywindow $title $message
	    } else {
		_sysnotify $title $message
	    }
	}
	"aqua" {
	    _sysnotify $title $message
	}
    }
}

#Add these commands to the tk command ensemble: tk systray, tk sysnotify
#Thanks to Christian Gollwitzer for the guidance here 
set map [namespace ensemble configure tk -map]
dict set map systray  ::systray
dict set map sysnotify ::sysnotify
namespace ensemble configure tk -map $map
