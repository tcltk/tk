# systray.tcl --

# This file defines the ::tk::systray command for icon display and manipulation
# in the system tray on X11, Windows, and macOS, and the ::tk::systnotify command
# for system alerts on each platform. It implements an abstraction layer that
# presents a consistent API across the three platforms. 

# Copyright (c) 2020 Kevin Walzer/WordTech Communications LLC. 
#
# See the file "license.terms" for information on usage and redistribution
# of this file, and for a DISCLAIMER OF ALL WARRANTIES.


# Pure-Tcl system tooltip window for use with system tray icon if native implementation not available. 

proc _balloon {w help} {
    bind $w <Any-Enter> "after 1000 [list _ballon_show %W [list $help]]"
    bind $w <Any-Leave> "destroy %W._balloon"
}

proc _balloon_show {w arg} {
    if {[eval winfo containing  [winfo pointerxy .]]!=$w} {return}
    set top $w._balloon
    catch {destroy $top}
    toplevel $top -bg black
    wm overrideredirect $top 1
    if {[string equal [tk windowingsystem] aqua]}  {
        ::tk::unsupported::MacWindowStyle style $top help none
    }   
    pack [message $top.txt -aspect 10000  \
	      -text $arg]
    set wmx [winfo rootx $w]
    set wmy [expr [winfo rooty $w]+[winfo height $w]]
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

image create photo _info -data {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}

proc _notifywindow {msg} {
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
    }
    message $w.message -aspect 150 -bg gray30 -fg white -aspect 150 -text $msg -width 280
    pack $w.message -side right -fill both -expand yes
    if {[tk windowingsystem] eq "x11"} {
        wm overrideredirect $w true
    }
    wm attributes $w -alpha 0.0
    set xpos [expr [winfo screenwidth $w] - 325]
    wm geometry $w +$xpos+30
    _fade_in $w
    after 3000 _fade_out $w
}

#Fade and destroy window.
proc fade_out {w} {
    catch {
        set prev_degree [wm attributes $w -alpha]
        set new_degree [expr $prev_degree - 0.05]
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
        set new_degree [expr $prev_degree + 0.05]
        set current_degree [wm attributes $w -alpha $new_degree]
        focus -force $w
        if {$new_degree < 0.9 && $new_degree != $prev_degree} {
        after 10 [list _fade_in $w]
    } else {
        return
        }
    }
}

global ico
set ico ""

# ::tk::systray --
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
proc ::tk::systray {args} {

   global ico
    
    #Create dict for system tray icon properties.
    set icondata [dict create]
    
    #Create the system tray icon.
    if {[lindex $args 0] eq "create"} {

        set img [lindex $args 1]
        set txt [lindex $args 2]
        set cb [lindex $args 3]

        dict set icondata text $txt
        dict set icondata image $img
        dict set icondata callback $cb

        switch -- [tk windowingsystem] {
            "win32" {
            set ico [_systray createfrom $img]    
            _systray taskbar add $ico -text $txt -callback [list _win_callback %m %i $cb]
                }
            "x11" {
            _systray ._tray -image $img -visible true
            _balloon ._tray $text
            bind [._tray bbox] <Button-1> [list $cb]
                }
	    "aqua" {
            _systray create $img $txt $cb
        }
	}
	#Modify the system tray icon properties. Call into Windows and X11 C commands.
	if {[lindex $args 0] eq "modify"} {
	    switch -- [tk windowingsystem] {
		"win32" {
		    if [lindex $args 1] eq "image" {
                set img [lindex $args 2]
                _systray taskbar delete $ico   
                set ico [_systray createfrom $img]
                dict set icondata image $img
                _systray taskbar add $ico -text [dict get $icondata text] -callback [list \
                                                    _win_callback %m %i [dict get $icondata callback]]
		    } 
		    if {[lindex $args 1] eq "text"} {
                set txt [lindex $args 2]
                dict set icondata text $txt
                _systray taskbar modify $ico -text $txt
		    }
		    if {[lindex $args 1 ] eq "callback"} {
                set cb [lindex $args 2]
                dict set icondata callback $cb
                _systray taskbar modify $ico -callback [list \
                                        _win_callback %m %i [dict get $icondata callback]]
		    }
		}
		"x11" {
		    if [lindex $args 1] eq "image" {
                set img [lindex $args 2]
                dict set icondata image $img
                ._tray configure -image $img 
		    } 
		    if {[lindex $args 1] eq "text"} {
                set txt [lindex $args 2]
                dict set icondata text $txt
                _balloon ._tray $text
		    }
		    if {[lindex $args 1 ] eq "callback"} {
                set cb [lindex $args 2]
                dict set icondata callback $cb
                bind [._tray bbox] <Button-1> [list $cb]
		    }
		}
        "aqua" {
            if [lindex $args 1] eq "image" {
                set img [lindex $args 2]
                dict set icondata image $img
               _systray modify image $img 
		    } 
		    if {[lindex $args 1] eq "text"} {
                set txt [lindex $args 2]
                dict set icondata text $txt
                _systray modify text $txt
		    }
		    if {[lindex $args 1 ] eq "callback"} {
                set cb [lindex $args 2]
                dict set icondata callback $cb
               _systray modify callback $cb
		    }
        }     
	}
	if {[lindex $args 0] eq "destroy"} {
		switch -- [tk windowingsystem] {
		    "win32" { 
			    _systray taskbar delete $ico 
		    }
		    "x11" {
			    destroy ._tray
		    }
         "aqua" {
	        _systray destroy
	        }
	    }
	}
}
	
# ::tk::sysnotify --
# This procedure implments a platform-specific system notification alert.
#   
#   Arguments: 
#       title - main text of alert. 
#       message - body text of alert.

proc ::tk::systray {title message} {

global ico

switch -- [tk windowingsystem] {
    "win32" {
    _sysnotify notify $ico $title $message
    }
    "x11" {
        if {![info exists _sysnotify]} {
            _notifywindow "$title\n\n$message"
        } else {
            _sysnotify $title $message
        }
    }
    "aqua" {
    _sysnotify $title $message
    }    
}









