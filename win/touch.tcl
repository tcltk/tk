proc rndCol {} {
    set lst {orange yellow green cyan blue purple violet pink}
    set i [expr {int(rand()*[llength $lst])}]
    return [lindex $lst $i]
}

proc Touch1 {d x y X Y} {
    set id [dict get $d id]

    set move [dict exists $d move]
    set down [dict exists $d down]
    set up   [dict exists $d up]
    set primary [dict exists $d primary]

    if {![info exists ::t($id,id)]} {
	set ::t($id,id) [.c1 create oval $x $y $x $y]
	if {$primary} {
	    .c1 itemconfigure $::t($id,id) -fill red
	} else {
	    .c1 itemconfigure $::t($id,id) -fill [rndCol]
	}
    }
    if {$up} {
	.c1 delete $::t($id,id)
	array unset ::t $id,*
	return
    }

    # Filter unnecessary movement
    if {$move && $x == $::t($id,x) && $y == $::t($id,y)} {
	return
    }
    set r [expr {[winfo screenwidth .] / 50}]
    set ::t($id,x) $x
    set ::t($id,y) $y
    .c1 coords $::t($id,id) [expr {$x - $r}] [expr {$y - $r}] \
	[expr {$x + $r}] [expr {$y + $r}]
}

proc Touch2 {d x y X Y} {
    set id [dict get $d id]
    # Gesture events do not do much yet
    .c2 delete gesture
    .c2 create text [expr {$::size / 2}] [expr {$::size / 2}] -text $d -tags gesture
    
}

#console show
set ::size [expr {[winfo screenwidth .] / 4}]
canvas .c1 -width $::size -height $::size -bd 3 -relief solid
canvas .c2 -width $::size -height $::size -bd 3 -relief solid
.c1 create text [expr {$::size * 0.5}] [expr {$::size *0.25}] -text Touch
.c2 create text [expr {$::size * 0.5}] [expr {$::size *0.25}] -text Gesture
wm touch .c1
pack .c1 .c2 -side left -fill both -expand 1
bind .c1 <Touch> "Touch1 %d %x %y %X %Y"
bind .c2 <Touch> "Touch2 %d %x %y %X %Y"
