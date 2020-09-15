# panedwindow.tcl --
#
# This file defines the default bindings for Tk panedwindow widgets and
# provides procedures that help in implementing those bindings.
#

bind Panedwindow <Button-1> { ::tk::panedwindow::MarkSash %W %x %y 1 }
bind Panedwindow <Button-2> { ::tk::panedwindow::MarkSash %W %x %y 0 }

bind Panedwindow <B1-Motion> { ::tk::panedwindow::DragSash %W %x %y 1 }
bind Panedwindow <B2-Motion> { ::tk::panedwindow::DragSash %W %x %y 0 }

bind Panedwindow <ButtonRelease-1> {::tk::panedwindow::ReleaseSash %W 1 %x %y}
bind Panedwindow <ButtonRelease-2> {::tk::panedwindow::ReleaseSash %W 0 %x %y}

bind Panedwindow <Motion>	{ ::tk::panedwindow::SetCursor %W %x %y }
bind Panedwindow <Enter>	{ ::tk::panedwindow::Enter %W %x %y }
bind Panedwindow <Leave>	{ ::tk::panedwindow::Leave %W }
bind PanedWindowChild <Enter>   { ::tk::panedwindow::SetCursor %W %x %y }

# Initialize namespace
namespace eval ::tk::panedwindow {}

# ::tk::panedwindow::MarkSash --
#
#   Handle marking the correct sash for possible dragging
#
# Arguments:
#   w		the widget
#   x		widget local x coord
#   y		widget local y coord
#   proxy	whether this should be a proxy sash
# Results:
#   None
#
proc ::tk::panedwindow::MarkSash {w x y proxy} {
    variable ::tk::Priv
    if {[$w cget -opaqueresize]} {
	set proxy 0
    }
    set what [$w identify $x $y]
    if { [llength $what] == 2 } {
	lassign $what index which
	if {!$::tk_strictMotif || $which eq "handle"} {
	    if {!$proxy} {
		$w sash mark $index $x $y
	    }
	    set Priv($w,sash) $index
	    lassign [$w sash coord $index] sx sy
	    set Priv($w,dx) [expr {$sx-$x}]
	    set Priv($w,dy) [expr {$sy-$y}]
	    # Do this to init the proxy location
	    DragSash $w $x $y $proxy
	}
    }
}

# ::tk::panedwindow::DragSash --
#
#   Handle dragging of the correct sash
#
# Arguments:
#   w		the widget
#   x		widget local x coord
#   y		widget local y coord
#   proxy	whether this should be a proxy sash
# Results:
#   Moves sash
#
proc ::tk::panedwindow::DragSash {w x y proxy} {
    variable ::tk::Priv
    if {[$w cget -opaqueresize]} {
	set proxy 0
    }
    if {[info exists Priv($w,sash)]} {
	if {$proxy} {
	    $w proxy place [expr {$x+$Priv($w,dx)}] [expr {$y+$Priv($w,dy)}]
	} else {
	    $w sash place $Priv($w,sash) \
		    [expr {$x+$Priv($w,dx)}] [expr {$y+$Priv($w,dy)}]
	}
    }
}

# ::tk::panedwindow::ReleaseSash --
#
#   Handle releasing of the sash
#
# Arguments:
#   w		the widget
#   proxy	whether this should be a proxy sash
# Results:
#   Returns ...
#
proc ::tk::panedwindow::ReleaseSash {w proxy x y} {
    variable ::tk::Priv
    if {[$w cget -opaqueresize]} {
	set proxy 0
    }
    if {[info exists Priv($w,sash)]} {
	if {$proxy} {
	    lassign [$w proxy coord] x y
	    $w sash place $Priv($w,sash) $x $y
	    $w proxy forget
	}
	unset Priv($w,sash) Priv($w,dx) Priv($w,dy)
    }
    SetCursor $w $x $y
}

proc ::tk::panedwindow::ResetCursor {w} {
    variable ::tk::Priv

    if { ! [info exists Priv($w,sash)] } {
      if {[info exists Priv($w,panecursor)]} {
          $w configure -cursor $Priv($w,panecursor)
          unset Priv($w,panecursor)
      }
    }
}

# On an <Enter> event for the main paned window,
# set up the "PanedWindowChild" bindings for the panedwindow's children.
#
# Call 'SetCursor' to save the current cursor.
#
proc ::tk::panedwindow::Enter {w x y} {
  variable ::tk::Priv

  set Priv($w,children) [winfo children $w]
  set Priv(panedwindowname) $w
  foreach child $Priv($w,children) {
    set Priv($child,oldbindtags) [bindtags $child]
    set bt $Priv($child,oldbindtags)
    lappend bt PanedWindowChild
    bindtags $child $bt
  }
  SetCursor $w $x $y
}

# When the pointer leaves the paned window,
# change the bindtags on the child windows back to
# what they were before.
#
# Reset the cursor.
#
proc ::tk::panedwindow::Leave {w} {
  variable ::tk::Priv

  foreach child $Priv($w,children) {
    set bt $Priv($child,oldbindtags)
    bindtags $child $bt
  }
  ResetCursor $w
}

# SetCursor checks to see if the pointer is over the panedwindow's sash.
# If so, change the cursor.
#
# If the cursor is not over the sash, or is over one of the paned window's
# children, reset the cursor back to the original.
#
# If the pointer is currently being dragged, do not change the cursor.
#
proc ::tk::panedwindow::SetCursor {currw x y} {
    variable ::tk::Priv

    set w $Priv(panedwindowname)

    if { [info exists Priv($w,sash)] } {
      # the sash is currently being dragged.  The
      # cursor should not be changed.
      return
    }
    if {![info exists Priv($w,panecursor)]} {
	set Priv($w,panecursor) [$w cget -cursor]
    }

    set cursor $Priv($w,panecursor)

    # The cursor only changes when the sash of the main window is
    # entered.  Otherwise the cursor is reset.
    set what {}
    if { $w eq $currw } {
      set what [$w identify $x $y]
    }
    if { [llength $what] == 2 } {
	lassign $what index which
	if {!$::tk_strictMotif || $which eq "handle"} {
	    if {[$w cget -sashcursor] ne ""} {
		set cursor [$w cget -sashcursor]
	    } elseif {[$w cget -orient] eq "horizontal"} {
		set cursor sb_h_double_arrow
	    } else {
		set cursor sb_v_double_arrow
	    }
        }
    }

    $w configure -cursor $cursor
}
