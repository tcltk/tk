# toggleswitch.tcl - Copyright © 2025 Csaba Nemethi <csaba.nemethi@t-online.de>
#
# Bindings for the ttk::toggleswitch widget.

namespace eval ttk::toggleswitch {
    variable State
    set State(dragging)  0
    set State(moveState) idle			;# other values: moving, moved
}

bind Toggleswitch <<ThemeChanged>>	{ ttk::toggleswitch::ThemeChanged %W }

if {![info exists tk::android] || !$tk::android} {
    bind Toggleswitch <Enter>	{ %W instate !disabled {%W state active} }
    bind Toggleswitch <Leave>	{ %W state !active }
}
bind Toggleswitch <B1-Leave>		{ # Preserves the active state. }
bind Toggleswitch <Button-1>		{ ttk::toggleswitch::Press   %W %x %y }
bind Toggleswitch <B1-Motion>		{ ttk::toggleswitch::Drag    %W %x %y }
bind Toggleswitch <ButtonRelease-1>	{ ttk::toggleswitch::Release       %W }
bind Toggleswitch <space>		{ ttk::toggleswitch::ToggleDelayed %W }

proc ttk::toggleswitch::ThemeChanged w {
    if {[info cmdtype $w] ne "native"} {
	return ""    ;# the widget was not created by the ttk::toggleswitch cmd
    }

    set stateSpec [$w state !disabled]			;# needed for $w set
    $w set [expr {[$w switchstate] ? [$w get max] : [$w get min]}]
    $w state $stateSpec					;# restores the state
}

proc ttk::toggleswitch::Press {w x y} {
     $w instate disabled {
	return ""
    }

    $w state pressed

    variable State
    array set State [list  dragging 0  moveState idle  startX $x  prevX $x \
	    prevElem [$w identify element $x $y]]
}

proc ttk::toggleswitch::Drag {w x y} {
    if {[$w instate disabled] || [$w instate !pressed]} {
	return ""
    }

    variable State

    if {[ttk::style theme use] eq "aqua"} {
	if {$State(moveState) eq "moving"} {
	    return ""
	}

	set curElem [$w identify element $x $y]
	if {[string match "*.slider" $State(prevElem)] &&
		[string match "*.trough" $curElem]} {
	    StartToggling $w
	} elseif {$x < 0} {
	    StartMovingLeft $w
	} elseif {$x >= [winfo width $w]} {
	    StartMovingRight $w
	}

	set State(prevElem) $curElem
    } else {
	if {!$State(dragging) && abs($x - $State(startX)) > [tk::ScaleNum 4]} {
	    set State(dragging) 1
	}
	if {!$State(dragging)} {
	    return ""
	}

	set curX [$w xcoord]
	set newX [expr {$curX + $x - $State(prevX)}]
	$w set [$w get $newX]

	set State(prevX) $x
    }
}

proc ttk::toggleswitch::Release w {
    if {[$w instate disabled] || [$w instate !pressed]} {
	return ""
    }

    variable State

    if {$State(dragging)} {
	$w switchstate [expr {[$w get] > [$w get max]/2}]
    } elseif {[$w instate hover]} {
	if {[ttk::style theme use] eq "aqua"} {
	    if {$State(moveState) eq "idle"} {
		StartToggling $w
	    }
	} else {
	    $w toggle
	}
    }

    $w state !pressed
    set State(dragging) 0
}

proc ttk::toggleswitch::ToggleDelayed w {
    if {[$w instate disabled] || [$w instate pressed]} {
	return ""
    }

    $w state pressed
    after 200 [list ttk::toggleswitch::ToggleSwitchState $w]
}

proc ttk::toggleswitch::StartToggling w {
    if {[$w get] == [$w get max]} {
	StartMovingLeft $w
    } else {
	StartMovingRight $w
    }
}

proc ttk::toggleswitch::StartMovingLeft w {
    if {[$w get] == [$w get min]} {
	return ""
    }

    variable State
    set State(moveState) moving
    $w state !selected		;# will be undone before invoking switchstate
    MoveLeft $w [$w get max]
}

proc ttk::toggleswitch::MoveLeft {w val} {
    if {![winfo exists $w] || [winfo class $w] ne "Toggleswitch"} {
	return ""
    }

    set val [expr {$val - 1}]
    $w set $val

    if {$val > [$w get min]} {
	after 10 [list ttk::toggleswitch::MoveLeft $w $val]
    } else {
	$w state selected	;# restores the original selected state
	$w switchstate 0

	variable State
	set State(moveState) moved
    }
}

proc ttk::toggleswitch::StartMovingRight w {
    if {[$w get] == [$w get max]} {
	return ""
    }

    variable State
    set State(moveState) moving
    $w state selected		;# will be undone before invoking switchstate
    MoveRight $w [$w get min]
}

proc ttk::toggleswitch::MoveRight {w val} {
    if {![winfo exists $w] || [winfo class $w] ne "Toggleswitch"} {
	return ""
    }

    set val [expr {$val + 1}]
    $w set $val

    if {$val < [$w get max]} {
	after 10 [list ttk::toggleswitch::MoveRight $w $val]
    } else {
	$w state !selected	;# restores the original !selected state
	$w switchstate 1

	variable State
	set State(moveState) moved
    }
}

proc ttk::toggleswitch::ToggleSwitchState w {
    if {![winfo exists $w] || [winfo class $w] ne "Toggleswitch"} {
	return ""
    }

    $w toggle
    $w state !pressed
}
