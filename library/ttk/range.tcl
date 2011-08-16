# range.tcl - Copyright (C) 2011 Goodwin Lawlor
#
# Bindings for the TRange widget

namespace eval ttk::range {
    variable State
    array set State  {
	dragging 0
    }
}

bind TRange <ButtonPress-1>   { ttk::range::Press %W %x %y }
bind TRange <B1-Motion>       { ttk::range::Drag %W %x %y }
bind TRange <ButtonRelease-1> { ttk::range::Release %W %x %y }

bind TRange <ButtonPress-2>   { ttk::range::Jump %W %x %y }
bind TRange <B2-Motion>       { ttk::range::Drag %W %x %y }
bind TRange <ButtonRelease-2> { ttk::range::Release %W %x %y }

bind TRange <ButtonPress-3>   { ttk::range::Jump %W %x %y }
bind TRange <B3-Motion>       { ttk::range::Drag %W %x %y }
bind TRange <ButtonRelease-3> { ttk::range::Release %W %x %y }

bind TRange <Left>            { ttk::range::Increment %W -1 }
bind TRange <Up>              { ttk::range::Increment %W -1 }
bind TRange <Right>           { ttk::range::Increment %W 1 }
bind TRange <Down>            { ttk::range::Increment %W 1 }
bind TRange <Control-Left>    { ttk::range::Increment %W -10 }
bind TRange <Control-Up>      { ttk::range::Increment %W -10 }
bind TRange <Control-Right>   { ttk::range::Increment %W 10 }
bind TRange <Control-Down>    { ttk::range::Increment %W 10 }
bind TRange <Home>            { %W set [%W cget -from] }
bind TRange <End>             { %W set [%W cget -to] }

proc ttk::range::Press {w x y} {
    variable State
    set State(dragging) 0

    switch -glob -- [$w identify $x $y] {
	*track -
        *trough {
	    set val [$w get $x $y]
	    set min [$w getmin]
	    set max [$w getmax]
	    if {$val < $min} {
		#increment min
		ttk::Repeatedly IncrementMin $w -0.1
	    } elseif {$val > $max} {
		#increment max
		ttk::Repeatedly IncrementMax $w +0.1
	    } else {
		set State(dragging) 3
		set State(initial) [$w get $x $y]
	    }
        }
        *minslider {
            set State(dragging) 1
            set State(initial) [$w getmin]
        }
	*maxslider {
	    # hack to prevent minslider hidding under the maxslider
	    # when both are at the "to" value
	    if {[$w getmin] == [$w cget -to]} {
		set State(dragging) 1
	    } else {
		set State(dragging) 2
	    }
            
            set State(initial) [$w getmax] 
	}
    }
}

# range::Jump -- ButtonPress-2/3 binding for range acts like
#	Press except that clicking in the trough jumps to the
#	clicked position.
proc ttk::range::Jump {w x y} {
    variable State
    set State(dragging) 0

    switch -glob -- [$w identify $x $y] {
	*track -
        *trough {
	    set val [$w get $x $y]
	    set min [$w getmin]
	    set max [$w getmax]
	    if {$val < $min} {
		#jump min
		$w setmin $val
		set State(initial) [$w get $x $y]
	    } elseif {$val > $max} {
		#jump max
		$w setmax $val
		set State(initial) [$w get $x $y]
	    } else {
		set State(dragging) 3
		set State(initial) [$w get $x $y]
	    }
        }
        *slider {
            Press $w $x $y
        }
    }
}

proc ttk::range::Drag {w x y} {
    variable State
    if {$State(dragging) eq 1} {
	$w setmin [$w get $x $y]
    } elseif {$State(dragging) eq 2} {
	$w setmax [$w get $x $y]
    } else {
	set v [$w get $x $y]
	set dv [expr $v - $State(initial)]
	$w setmin [expr [$w getmin] + $dv]
	$w setmax [expr [$w getmax] + $dv]
	set State(initial) $v
    }
}

proc ttk::range::Release {w x y} {
    variable State
    set State(dragging) 0
    ttk::CancelRepeat
}

proc ttk::range::IncrementMin {w delta} {
    if {![winfo exists $w]} return
    $w setmin [expr {[$w getmin] + $delta}]
}

proc ttk::range::IncrementMax {w delta} {
    if {![winfo exists $w]} return
    $w setmax [expr {[$w getmax] + $delta}]
}
