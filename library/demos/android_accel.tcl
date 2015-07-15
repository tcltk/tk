# Android demo for <<Accelerometer>> event

proc showaccel {canvas axis value} {
    set ix 0
    set iy 0
    if {$axis == 1} {
	set ix [expr {$value / 256}]
    } elseif {$axis == 2} {
	set iy [expr {$value / 256}]
    } elseif {$axis == 3} {
	set ::pos(t) [expr {($value / 256) % 360}]
    } else {
	return
    }
    if {![info exists ::pos(x)]} {
	set ::pos(x) [expr [winfo width $canvas] / 4]
	set ::pos(y) [expr [winfo height $canvas] / 4]
	set ::pos(t) 0
    }
    set ::pos(x) [expr {$::pos(x) + $ix}] 
    set ::pos(y) [expr {$::pos(y) + $iy}] 
    if {$::pos(x) < 50} {
	set ::pos(x) 50
    } elseif {$::pos(x) > [winfo width $canvas] - 50} {
	set ::pos(x) [expr {[winfo width $canvas] - 50}]
    }
    if {$::pos(y) < 50} {
	set ::pos(y) 50
    } elseif {$::pos(y) > [winfo height $canvas] - 50} {
	set ::pos(y) [expr {[winfo height $canvas] - 50}]
    }
    if {$axis == 3} {
	$canvas delete a
	set x0 [expr {$::pos(x) - 48}]
	set x1 [expr {$x0 + 96}]
	set y0 [expr {$::pos(y) - 48}]
	set y1 [expr {$y0 + 96}]
	$canvas create arc $x0 $y0 $x1 $y1 -fill yellow -outline red \
	    -width 6 -start [expr {330 - $::pos(t)}] -extent -300.0 -tags a
	set y2 [expr $y1 + 44]
	$canvas create text $::pos(x) $y2 -fill white -justify center \
	    -angle $::pos(t) -text "Accelerometer\nDemo" \
	    -tags a -font {{DejaVu Sans} 14 bold}
    }
}

wm attributes . -fullscreen 1
. configure -bg black
canvas .c -bg black -bd 0 -highlightthickness 0
pack .c -side top -fill both -expand 1 -padx 0 -pady 0
set f [open [info script]]
.c create text 20 120 -anchor nw -tag s -font {Courier 5} -text [read $f] \
    -fill gray50
close $f
button .c.x -text Exit -command {
    exit 0
}
bind .c.x <Return> {tk::ButtonInvoke %W}
.c create window 30 60 -anchor nw -tag x -window .c.x
bind . <<Accelerometer>> {showaccel .c %s %x}
sdltk accelerometer on
