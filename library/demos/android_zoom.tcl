# Android demo for <<PinchToZoom>> event

proc showzoom {canvas rootx rooty dist angle state} {
    $canvas itemconf t -text "XY: $rootx,$rooty L: $dist P: $angle S: $state"
    $canvas delete a
    # state 0 -> zoom motion
    # state 1 -> zoom start
    # state 2 -> zoom end, 1st finger up
    # state 3 -> zoom end, 2nd finger up
    if {$state < 2} {
	set phi [expr {$angle / 64.0}]
	set x0 [expr {$rootx - [winfo rootx $canvas] - $dist / 2}]
	set x1 [expr {$x0 + $dist}]
	set y0 [expr {$rooty - [winfo rooty $canvas] - $dist / 2}]
	set y1 [expr {$y0 + $dist}]
	$canvas create arc $x0 $y0 $x1 $y1 -fill yellow -outline red -width 6 \
	    -start [expr {330 - $phi}] -extent -300.0 -tags a
    }
}

wm attributes . -fullscreen 1
. configure -bg black
sdltk touchtranslate 15 ;# turn <<PinchToZoom>> on
canvas .c -bg black -bd 0 -highlightthickness 0
pack .c -side top -fill both -expand 1 -padx 0 -pady 0
set f [open [info script]]
.c create text 30 120 -anchor nw -tag s -font {Courier 5} -text [read $f] \
    -fill gray50
close $f
.c create text 30 30 -anchor w -fill green -tag t -font {Helvetica 15} \
    -text "Try pinch-to-zoom with two fingers"
button .c.x -text Exit -command {exit 0}
.c create window 30 60 -anchor nw -tag x -window .c.x
bind .c <<PinchToZoom>> {showzoom %W %X %Y %x %y %s}
