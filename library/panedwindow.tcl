# panedwindow.tcl --
#
# This file defines the default bindings for Tk panedwindow widgets and
# provides procedures that help in implementing those bindings.
#
# RCS: @(#) $Id: panedwindow.tcl,v 1.1 2002/02/22 02:41:17 hobbs Exp $
#

bind PanedWindow <Button-1> { ::tk::panedwindow::MarkSash %W %x %y 1 }
bind PanedWindow <Button-2> { ::tk::panedwindow::MarkSash %W %x %y 0 }

bind PanedWindow <B1-Motion> { ::tk::panedwindow::DragSash %W %x %y 1 }
bind PanedWindow <B2-Motion> { ::tk::panedwindow::DragSash %W %x %y 0 }

bind PanedWindow <ButtonRelease-1> {::tk::panedwindow::ReleaseSash %W %x %y 1}
bind PanedWindow <ButtonRelease-2> {::tk::panedwindow::ReleaseSash %W %x %y 0}

bind PanedWindow <Motion> { ::tk::panedwindow::Motion %W %x %y }

bind PanedWindow <Leave> { ::tk::panedwindow::Leave %W }

# Initialize namespace
namespace eval ::tk::panedwindow {}

# ::tk::panedwindow::MarkSash --
#
#   ADD COMMENTS HERE
#
# Arguments:
#   args	comments
# Results:
#   Returns ...
#
proc ::tk::panedwindow::MarkSash {w x y proxy} {
    set what [$w identify $x $y]
    if { [llength $what] == 2 } {
	foreach {index which} $what break
	if { !$::tk_strictMotif || [string equal $which "handle"] } {
	    if {!$proxy} { $w sash mark $index $x $y }
	    set ::tk::Priv(sash) $index
	}
    }
}

# ::tk::panedwindow::DragSash --
#
#   ADD COMMENTS HERE
#
# Arguments:
#   args	comments
# Results:
#   Returns ...
#
proc ::tk::panedwindow::DragSash {w x y proxy} {
    if { [info exists ::tk::Priv(sash)] } {
	if {$proxy} {
	    $w proxy place $x $y
	} else {
	    $w sash dragto $::tk::Priv(sash) $x $y
	    $w sash mark $::tk::Priv(sash) $x $y
	}
    }
}

# ::tk::panedwindow::ReleaseSash --
#
#   ADD COMMENTS HERE
#
# Arguments:
#   args	comments
# Results:
#   Returns ...
#
proc ::tk::panedwindow::ReleaseSash {w proxy} {
    if { [info exists ::tk::Priv(sash)] } {
	if {$proxy} {
	    foreach {x y} [$w proxy coord] break
	    $w sash place $::tk::Priv(sash) $x $y
	    unset ::tk::Priv(sash)
	    $w proxy forget
	} else {
	    unset ::tk::Priv(sash)
	}
    }
}

# ::tk::panedwindow::Motion --
#
#   ADD COMMENTS HERE
#
# Arguments:
#   args	comments
# Results:
#   Returns ...
#
proc ::tk::panedwindow::Motion {w x y} {
    variable ::tk::Priv
    set id [$w identify $x $y]
    if { [llength $id] == 2 } {
	if { !$::tk_strictMotif || [string equal [lindex $id 1] "handle"] } {
	    if { ![info exists Priv(panecursor)] } {
		set Priv(panecursor) [$w cget -cursor]
	    }
	    if { [string equal [$w cget -sashcursor] ""] } {
		if { [string equal [$w cget -orient] "horizontal"] } {
		    $w configure -cursor sb_h_double_arrow
		} else {
		    $w configure -cursor sb_v_double_arrow
		}
	    } else {
		$w configure -cursor [$w cget -sashcursor]
	    }
	    return
	}
    }
    if { [info exists Priv(panecursor)] } {
	$w configure -cursor $Priv(panecursor)
	unset Priv(panecursor)
    }
}

# ::tk::panedwindow::Leave --
#
#   ADD COMMENTS HERE
#
# Arguments:
#   args	comments
# Results:
#   Returns ...
#
proc ::tk::panedwindow::Leave {w} {
    if { [info exists ::tk::Priv(panecursor)] } {
        $w configure -cursor $::tk::Priv(panecursor)
        unset ::tk::Priv(panecursor)
    }
}
