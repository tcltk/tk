proc rndCol {} {
    set lst {orange yellow green cyan blue purple violet pink}
    set i [expr {int(rand()*[llength $lst])}]
    return [lindex $lst $i]
}

proc Touch {d x y X Y} {
    set id [dict get $d id]
    set flags [dict get $d flags]

    set move [expr {!!($flags & 1)}]
    set down [expr {!!($flags & 2)}]
    set up   [expr {!!($flags & 4)}]
    set primary [expr {!!($flags & 16)}]

    if {![info exists ::t($id,id)]} {
	set ::t($id,id) [.c create oval $x $y $x $y]
	if {$primary} {
	    .c itemconfigure $::t($id,id) -fill red
	} else {
	    .c itemconfigure $::t($id,id) -fill [rndCol]
	}
    }
    if {$up} {
	.c delete $::t($id,id)
	array unset ::t $id,*
	return
    }

    # Filter unnecessary movement
    if {$move && $x == $::t($id,x) && $y == $::t($id,y)} {
	return
    }
    set r 50
    set ::t($id,x) $x
    set ::t($id,y) $y
    .c coords $::t($id,id) [expr {$x - $r}] [expr {$y - $r}] [expr {$x + $r}] [expr {$y + $r}]

}

#console show
canvas .c -width 800 -height 800
wm touch .c
pack .c -fill both -expand 1
bind .c <Touch> "Touch %d %x %y %X %Y"
