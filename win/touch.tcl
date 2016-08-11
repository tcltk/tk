namespace import tcl::mathop::*

proc rndCol {} {
    set lst {orange yellow green cyan blue purple violet pink}
    set i [expr {int(rand()*[llength $lst])}]
    return [lindex $lst $i]
}

proc Circle {w x y r args} {
    $w create oval [- $x $r] [- $y $r] [+ $x $r] [+ $y $r] \
	-fill "" -outline black -width 2 {*}$args
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

proc Log {d} {
    # Make a little log of messages for now
    if {[lindex $::messages 9] ne $d} {
	lappend ::messages $d
	set ::messages [lrange $::messages end-9 end]
    }
    set txt [join $::messages \n]
    .c2 itemconfigure gesture -text $txt
}

proc Touch2 {d x y X Y} {
    set id [dict get $d id]

    if {[dict exists $d twofingertap]} {
	.c2 delete twofingertap
	set r [expr {[dict get $d distance] / 2}]
	Circle .c2 $x $y $r -fill yellow -tags twofingertap
	return
    }
    if {[dict exists $d pressandtap]} {
	if {[dict exists $d begin]} {
	    # Only the begin message has delta set
	    .c2 delete pressandtap
	    set x1 [expr {$x + [dict get $d deltax]}]
	    set y1 [expr {$y + [dict get $d deltay]}]
	    .c2 create line $x $y $x1 $y1 -width 5 -fill red -tags pressandtap
	}
	if {[dict exists $d end]} {
	    # Make end visible by changing colour
	    .c2 itemconfigure pressandtap -fill purple
	}
	return
    }
    if {[dict exists $d zoom]} {
	.c2 delete zoom
	set r [expr {[dict get $d distance] / 2}]
	Circle .c2 $x $y $r -fill blue -tags zoom
	return
    }
    if {[dict exists $d pan]} {
	.c2 delete pan
	set r [expr {[dict get $d distance] / 2}]
	Circle .c2 $x $y $r -fill green -tags pan
	return
    }
    if {[dict exists $d rotate]} {
	.c2 delete rotate
	set a [expr {180.0*[dict get $d angle]/3.141592 - 10}]
	set r 100
	.c2 create arc [- $x $r] [- $y $r] [+ $x $r] [+ $y $r] \
	    -fill orange -outline black -width 2 -tags rotate \
	    -start $a -extent 20
	return
    }
    Log $d
}

#console show
set ::size [expr {[winfo screenwidth .] / 4}]
canvas .c1 -width $::size -height $::size -bd 3 -relief solid
canvas .c2 -width $::size -height $::size -bd 3 -relief solid
.c1 create text [expr {$::size /2}] [expr {$::size /2}] -text Touch
lappend ::messages "Gesture"
.c2 create text [expr {$::size / 2}] [expr {$::size / 2}] -text Gesture -tags gesture

pack .c1 .c2 -side left -fill both -expand 1
wm touch .c1
bind .c1 <Touch> "Touch1 %d %x %y %X %Y"
bind .c2 <Touch> "Touch2 %d %x %y %X %Y"
