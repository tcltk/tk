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
    pack [message $top._txt -aspect 10000  \
	      -text $arg]
    set wmx [winfo rootx $w]
    set wmy [expr {[winfo rooty $w] + [winfo height $w]}]
    wm geometry $top [winfo reqwidth $top._txt]x[
						winfo reqheight $top._txt]+$wmx+$wmy
    raise $top
}

# Additional infrastructure for Windows callbacks.
proc _win_callback {msg icn script} {
    global _cb_1
    global _cb_3
    
    switch -exact -- $msg {
	WM_LBUTTONDOWN {
	    eval $_cb_3
	}
	WM_RBUTTONDOWN {
	    eval $_cb_1
	}
    }
}

# Pure-Tcl system notification window for use if native implementation not available.

image create photo _info -data {
R0lGODlhIAAgAKUAAERq5KS29HSS7NTe/Fx+5Iym7Ozy/LzK9Ex25ISe7OTq/GyK5JSu7Pz6/Mza9Exy5Ky+9ISa7GSG5Fx65Exu5Hya7OTm/GSC5PTy/MTS9FR25Ozu/Jyu7ERu5KS69HyW7Nzi/FyC5JSq7MTO9Iyi7OTu/HSO7Pz+/NTa9LTC9PT2/FR65Jyy7P///wAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAACH5BAEAAC0ALAAAAAAgACAAAAb9wJZwSDw5WAKCBkEwMTINonQ6xLA0gKx2+xBtqNQT5LEtlzusKFioEpjfZckXbLjA71qNhaqS4P8ACHNDJx94Cw4ODHghakIpeCsnQwV4DEMqZHcFRA5/CkIBfwlEB6MtJyuADkIGIYAqA4BZAglYgAces7taCRGzIRLCCIDBgAtEEIAdBIAmycvNf89DyoB+09B/HRXO2oy62dWACbLiQtZ4KannLel3Bi2ieNTofx9sHfTfcCBDkHfqucNDosiCOw8qKKyA7Y0GR61e8XoDasoGaRO1+KNzMKOGEmuEnAi3qwDEkAYK6MOToGLIKQ0yiFigQR8CCQUOqAgZBAA7
}

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
global _iconlist
set _iconlist {}
global _cb_1
set _cb_1 ""
global _cb_3
set _cb_3 ""

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
	error "wrong # args: should be \"tk systray create | modify | destroy\""
    }

    #Set variables for icon properties.
    global _ico
    global _iconlist
    global _cb_1
    global _cb_3

    set _img ""
    set _txt ""
    set _cb_1 ""
    set _cb_3 ""

    #Remove the systray icon.
    if {[lindex $args 0] eq "destroy" && [llength $args] == 1} {
	switch -- [tk windowingsystem] {
	    "win32" {
		set _iconlist {}
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
	if {[llength $args] != 5} {
	    error "wrong # args: should be \"tk systray create image ?text? ?b1_callback? b3_callback?\""
	}
	set _img [lindex $args 1]
	set _txt [lindex $args 2]
	set _cb_1 [lindex $args 3]
	set _cb_3 [lindex $args 4]
	switch -- [tk windowingsystem] {
	    "win32" {
		if {[llength $_iconlist] > 0} {
		    error "Only one system tray \
		    icon supported per interpeter"
		}
		set _ico [_systray createfrom $_img]
		_systray taskbar add $_ico -text $_txt -callback [list _win_callback %m %i]
		lappend _iconlist "ico#[llength _iconlist]"
	    }
	    "x11" {
		if [winfo exists ._tray] {
		    error  "Only one system tray \
		    icon supported per interpeter"
		}
		set _ico $_img
		_systray ._tray -image $_img -visible true
		_balloon ._tray $_txt
		bind ._tray <Button-1> $_cb_1
		bind ._tray <Button-3> $_cb_3
	    }
	    "aqua" {
		_systray create $_img $_txt $_cb_1 $_cb_3
	    }
	}
    }
    #Modify the system tray icon properties.
    if {[lindex $args 0] eq "modify"} {
	if {[llength $args] != 3} {
	    error "wrong # args: \"tk systray modify image | text | b1_callback | b3_callback option?\""
	}
	switch -- [tk windowingsystem] {
	    "win32" {
		if {[lindex $args 1] eq "image"} {
		    set _img [lindex $args 2]
		    _systray taskbar delete $_ico
		    set _ico [_systray createfrom $_img]
		    _systray taskbar add $_ico -text $_txt -callback [list _win_callback %m %i]
		}
		if {[lindex $args 1] eq "text"} {
		    set _txt [lindex $args 2]
		    _systray taskbar modify $_ico -text $_txt
		}
		if {[lindex $args 1 ] eq "b1_callback"} {
		    set _cb_1 [lindex $args 2]
		    _systray taskbar modify $_ico -callback [list _win_callback %m %i]
		}
		if {[lindex $args 1 ] eq "b3_callback"} {
		    set _cb_3 [lindex $args 2]
		    _systray taskbar modify $_ico -callback [list _win_callback %m %i]
		}
		
	    }
	    "x11" {
		if {[lindex $args 1] eq "image"} {
		    set _img [lindex $args 2]
		    ._tray configure -image ""
		    ._tray configure -image $_img
		    set _ico $_img
		}
		if {[lindex $args 1] eq "text"} {
		    set _txt ""
		    set _txt [lindex $args 2]
		    _balloon ._tray $_txt
		}
		if {[lindex $args 1 ] eq "b1_callback"} {
		    set _cb_1 ""
		    bind ._tray <Button-1> ""
		    set _cb_1 [lindex $args 2]
		    bind ._tray <Button-1>  $_cb_1
		}
		if {[lindex $args 1 ] eq "b3_callback"} {
		    set _cb_3 ""
		    bind ._tray <Button-3> ""
		    set _cb_3 [lindex $args 2]
		    bind ._tray <Button-3>  $_cb_3
		}
	    }
	    "aqua" {
		if {[lindex $args 1] eq "image"} {
		    set _img [lindex $args 2]
		    _systray modify image $_img
		}
		if {[lindex $args 1] eq "text"} {
		    set _txt [lindex $args 2]
		    _systray modify text $_txt
		}
		if {[lindex $args 1 ] eq "b1_callback"} {
		    set _cb_1 [lindex $args 2]
		    _systray modify b1_callback $_cb_1
		}
		if {[lindex $args 1 ] eq "b3_callback"} {
		    set _cb_3 [lindex $args 2]
		    _systray modify b3_callback $_cb_3
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

proc ::tk::sysnotify {title message} {

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
    return
}

#Add these commands to the tk command ensemble: tk systray, tk sysnotify
#Thanks to Christian Gollwitzer for the guidance here 
set map [namespace ensemble configure tk -map]
dict set map systray  ::tk::systray
dict set map sysnotify ::tk::sysnotify
namespace ensemble configure tk -map $map
