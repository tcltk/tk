# Android demo for accelerometer buffer

package require Borg

borg screenorientation landscape
sdltk screensaver 0
wm attributes . -fullscreen 1
bind all <Break> exit

proc make_widgets {} {
    array set dm [borg displaymetrics]
    set w [expr {int($dm(width) / 3.2)}]
    pack [label .title -text "Seismograph"] -side top -pady 10
    pack [frame .gain] -side bottom -pady 20
    pack [label .gain.label -text "Gain" -width 6] -side left -padx 10
    pack [scale .gain.gain -from -6 -to 30 -length 330 -orient horizontal \
	-variable GAIN(value) -resolution 6 -command set_gain -showvalue 0] \
	-side left
    pack [label .gain.current -width 6 -textvariable GAIN(text)] -side left \
	-padx 10
    pack [canvas .c1 -height 300 -width $w -background black] \
	-padx 5 -pady 5 -side left -expand 1
    pack [canvas .c2 -height 300 -width $w -background black] \
	-padx 5 -pady 5 -side left -expand 1
    pack [canvas .c3 -height 300 -width $w -background black] \
	-padx 5 -pady 5 -side left -expand 1
}

proc set_gain {value} {
    set ::GAIN(gain) [expr {round($value / 6)}]
    set value [expr {$::GAIN(gain) * 6}]
    set ::GAIN(text) [format "%3d db" $value]
    .c1 delete all
    .c2 delete all
    .c3 delete all
}

proc draw_buffer {axis buffer} {
    .c$axis move all 0 -50
    set coords [.c$axis coords all]
    set y [llength $coords]
    if {$y >= [winfo height .c$axis] * 2} {
	set coords [lreplace $coords 0 99]
	set y [expr {($y - 100) / 2}]
    } else {
	set y [expr {[winfo height .c$axis] - 50}]
    }
    set w [expr {[winfo width .c$axis] / 2}]
    set d 512
    if {$::GAIN(gain) >= 0} {
	set d [expr {512 >> ($::GAIN(gain) + 1)}]
    }
    set d [expr {1.0 * $d}]
    foreach x $buffer {
	lappend coords [expr {$w + $x / $d}] $y
	incr y
    }
    .c$axis delete all
    .c$axis create line $coords -width 0 -fill green
}

proc read_all {} {
    after cancel read_all
    after 1000 read_all
    set b1 [sdltk accelbuffer 1]
    set b2 [sdltk accelbuffer 2]
    set b3 [sdltk accelbuffer 3]
    draw_buffer 1 $b1
    draw_buffer 2 $b2
    draw_buffer 3 $b3
}

make_widgets
set_gain 0
read_all
