# spinbox.tcl --
#
# This file defines the default bindings for Tk spinbox widgets and provides
# procedures that help in implementing those bindings.
#
# RCS: @(#) $Id: spinbox.tcl,v 1.3 2001/08/01 16:21:11 dgp Exp $
#
# Copyright (c) 1992-1994 The Regents of the University of California.
# Copyright (c) 1994-1997 Sun Microsystems, Inc.
# Copyright (c) 1999-2000 Jeffrey Hobbs
# Copyright (c) 2000 Ajuba Solutions
#
# See the file "license.terms" for information on usage and redistribution
# of this file, and for a DISCLAIMER OF ALL WARRANTIES.
#

#-------------------------------------------------------------------------
# Elements of tk::Priv that are used in this file:
#
# afterId -		If non-null, it means that auto-scanning is underway
#			and it gives the "after" id for the next auto-scan
#			command to be executed.
# mouseMoved -		Non-zero means the mouse has moved a significant
#			amount since the button went down (so, for example,
#			start dragging out a selection).
# pressX -		X-coordinate at which the mouse button was pressed.
# selectMode -		The style of selection currently underway:
#			char, word, or line.
# x, y -		Last known mouse coordinates for scanning
#			and auto-scanning.
# data -		Used for Cut and Copy
#-------------------------------------------------------------------------

# Initialize namespace
namespace eval ::tk::spinbox {}

#-------------------------------------------------------------------------
# The code below creates the default class bindings for entries.
#-------------------------------------------------------------------------
bind Spinbox <<Cut>> {
    if {![catch {::tk::spinbox::GetSelection %W} tk::Priv(data)]} {
	clipboard clear -displayof %W
	clipboard append -displayof %W $tk::Priv(data)
	%W delete sel.first sel.last
	unset tk::Priv(data)
    }
}
bind Spinbox <<Copy>> {
    if {![catch {::tk::spinbox::GetSelection %W} tk::Priv(data)]} {
	clipboard clear -displayof %W
	clipboard append -displayof %W $tk::Priv(data)
	unset tk::Priv(data)
    }
}
bind Spinbox <<Paste>> {
    global tcl_platform
    catch {
	if {[string compare $tcl_platform(platform) "unix"]} {
	    catch {
		%W delete sel.first sel.last
	    }
	}
	%W insert insert [::tk::GetSelection %W CLIPBOARD]
	::tk::spinbox::SeeInsert %W
    }
}
bind Spinbox <<Clear>> {
    %W delete sel.first sel.last
}
bind Spinbox <<PasteSelection>> {
    if {!$tk::Priv(mouseMoved) || $tk_strictMotif} {
	::tk::spinbox::Paste %W %x
    }
}

# Standard Motif bindings:

bind Spinbox <1> {
    ::tk::spinbox::ButtonDown %W %x %y
}
bind Spinbox <B1-Motion> {
    ::tk::spinbox::Motion %W %x %y
}
bind Spinbox <Double-1> {
    set tk::Priv(selectMode) word
    ::tk::spinbox::MouseSelect %W %x sel.first
}
bind Spinbox <Triple-1> {
    set tk::Priv(selectMode) line
    ::tk::spinbox::MouseSelect %W %x 0
}
bind Spinbox <Shift-1> {
    set tk::Priv(selectMode) char
    %W selection adjust @%x
}
bind Spinbox <Double-Shift-1> {
    set tk::Priv(selectMode) word
    ::tk::spinbox::MouseSelect %W %x
}
bind Spinbox <Triple-Shift-1> {
    set tk::Priv(selectMode) line
    ::tk::spinbox::MouseSelect %W %x
}
bind Spinbox <B1-Leave> {
    set tk::Priv(x) %x
    ::tk::spinbox::AutoScan %W
}
bind Spinbox <B1-Enter> {
    tk::CancelRepeat
}
bind Spinbox <ButtonRelease-1> {
    ::tk::spinbox::ButtonUp %W %x %y
}
bind Spinbox <Control-1> {
    %W icursor @%x
}

bind Spinbox <Up> {
    %W invoke buttonup
}
bind Spinbox <Down> {
    %W invoke buttondown
}

bind Spinbox <Left> {
    ::tk::spinbox::SetCursor %W [expr {[%W index insert] - 1}]
}
bind Spinbox <Right> {
    ::tk::spinbox::SetCursor %W [expr {[%W index insert] + 1}]
}
bind Spinbox <Shift-Left> {
    ::tk::spinbox::KeySelect %W [expr {[%W index insert] - 1}]
    ::tk::spinbox::SeeInsert %W
}
bind Spinbox <Shift-Right> {
    ::tk::spinbox::KeySelect %W [expr {[%W index insert] + 1}]
    ::tk::spinbox::SeeInsert %W
}
bind Spinbox <Control-Left> {
    ::tk::spinbox::SetCursor %W [::tk::spinbox::PreviousWord %W insert]
}
bind Spinbox <Control-Right> {
    ::tk::spinbox::SetCursor %W [::tk::spinbox::NextWord %W insert]
}
bind Spinbox <Shift-Control-Left> {
    ::tk::spinbox::KeySelect %W [::tk::spinbox::PreviousWord %W insert]
    ::tk::spinbox::SeeInsert %W
}
bind Spinbox <Shift-Control-Right> {
    ::tk::spinbox::KeySelect %W [::tk::spinbox::NextWord %W insert]
    ::tk::spinbox::SeeInsert %W
}
bind Spinbox <Home> {
    ::tk::spinbox::SetCursor %W 0
}
bind Spinbox <Shift-Home> {
    ::tk::spinbox::KeySelect %W 0
    ::tk::spinbox::SeeInsert %W
}
bind Spinbox <End> {
    ::tk::spinbox::SetCursor %W end
}
bind Spinbox <Shift-End> {
    ::tk::spinbox::KeySelect %W end
    ::tk::spinbox::SeeInsert %W
}

bind Spinbox <Delete> {
    if {[%W selection present]} {
	%W delete sel.first sel.last
    } else {
	%W delete insert
    }
}
bind Spinbox <BackSpace> {
    ::tk::spinbox::Backspace %W
}

bind Spinbox <Control-space> {
    %W selection from insert
}
bind Spinbox <Select> {
    %W selection from insert
}
bind Spinbox <Control-Shift-space> {
    %W selection adjust insert
}
bind Spinbox <Shift-Select> {
    %W selection adjust insert
}
bind Spinbox <Control-slash> {
    %W selection range 0 end
}
bind Spinbox <Control-backslash> {
    %W selection clear
}
bind Spinbox <KeyPress> {
    ::tk::spinbox::Insert %W %A
}

# Ignore all Alt, Meta, and Control keypresses unless explicitly bound.
# Otherwise, if a widget binding for one of these is defined, the
# <KeyPress> class binding will also fire and insert the character,
# which is wrong.  Ditto for Escape, Return, and Tab.

bind Spinbox <Alt-KeyPress> {# nothing}
bind Spinbox <Meta-KeyPress> {# nothing}
bind Spinbox <Control-KeyPress> {# nothing}
bind Spinbox <Escape> {# nothing}
bind Spinbox <Return> {# nothing}
bind Spinbox <KP_Enter> {# nothing}
bind Spinbox <Tab> {# nothing}
if {[string equal $tcl_platform(platform) "macintosh"]} {
	bind Spinbox <Command-KeyPress> {# nothing}
}

# On Windows, paste is done using Shift-Insert.  Shift-Insert already
# generates the <<Paste>> event, so we don't need to do anything here.
if {[string compare $tcl_platform(platform) "windows"]} {
    bind Spinbox <Insert> {
	catch {::tk::spinbox::Insert %W [::tk::GetSelection %W PRIMARY]}
    }
}

# Additional emacs-like bindings:

bind Spinbox <Control-a> {
    if {!$tk_strictMotif} {
	::tk::spinbox::SetCursor %W 0
    }
}
bind Spinbox <Control-b> {
    if {!$tk_strictMotif} {
	::tk::spinbox::SetCursor %W [expr {[%W index insert] - 1}]
    }
}
bind Spinbox <Control-d> {
    if {!$tk_strictMotif} {
	%W delete insert
    }
}
bind Spinbox <Control-e> {
    if {!$tk_strictMotif} {
	::tk::spinbox::SetCursor %W end
    }
}
bind Spinbox <Control-f> {
    if {!$tk_strictMotif} {
	::tk::spinbox::SetCursor %W [expr {[%W index insert] + 1}]
    }
}
bind Spinbox <Control-h> {
    if {!$tk_strictMotif} {
	::tk::spinbox::Backspace %W
    }
}
bind Spinbox <Control-k> {
    if {!$tk_strictMotif} {
	%W delete insert end
    }
}
bind Spinbox <Control-t> {
    if {!$tk_strictMotif} {
	::tk::spinbox::Transpose %W
    }
}
bind Spinbox <Meta-b> {
    if {!$tk_strictMotif} {
	::tk::spinbox::SetCursor %W [::tk::spinbox::PreviousWord %W insert]
    }
}
bind Spinbox <Meta-d> {
    if {!$tk_strictMotif} {
	%W delete insert [::tk::spinbox::NextWord %W insert]
    }
}
bind Spinbox <Meta-f> {
    if {!$tk_strictMotif} {
	::tk::spinbox::SetCursor %W [::tk::spinbox::NextWord %W insert]
    }
}
bind Spinbox <Meta-BackSpace> {
    if {!$tk_strictMotif} {
	%W delete [::tk::spinbox::PreviousWord %W insert] insert
    }
}
bind Spinbox <Meta-Delete> {
    if {!$tk_strictMotif} {
	%W delete [::tk::spinbox::PreviousWord %W insert] insert
    }
}

# A few additional bindings of my own.

bind Spinbox <2> {
    if {!$tk_strictMotif} {
	%W scan mark %x
	set tk::Priv(x) %x
	set tk::Priv(y) %y
	set tk::Priv(mouseMoved) 0
    }
}
bind Spinbox <B2-Motion> {
    if {!$tk_strictMotif} {
	if {abs(%x-$tk::Priv(x)) > 2} {
	    set tk::Priv(mouseMoved) 1
	}
	%W scan dragto %x
    }
}

# ::tk::spinbox::Invoke --
# Invoke an element of the spinbox
#
# Arguments:
# w -		The spinbox window.
# elem -	Element to invoke

proc ::tk::spinbox::Invoke {w elem} {
    variable ::tk::Priv

    if {![info exists Priv(outsideElement)]} {
	$w invoke $elem
	incr Priv(repeated)
    }
    set delay [$w cget -repeatinterval]
    if {$delay > 0} {
	set Priv(afterId) [after $delay \
		[list ::tk::spinbox::Invoke $w $elem]]
    }
}

# ::tk::spinbox::ClosestGap --
# Given x and y coordinates, this procedure finds the closest boundary
# between characters to the given coordinates and returns the index
# of the character just after the boundary.
#
# Arguments:
# w -		The spinbox window.
# x -		X-coordinate within the window.

proc ::tk::spinbox::ClosestGap {w x} {
    set pos [$w index @$x]
    set bbox [$w bbox $pos]
    if {($x - [lindex $bbox 0]) < ([lindex $bbox 2]/2)} {
	return $pos
    }
    incr pos
}

# ::tk::spinbox::ButtonDown --
# This procedure is invoked to handle button-1 presses in spinbox
# widgets.  It moves the insertion cursor, sets the selection anchor,
# and claims the input focus.
#
# Arguments:
# w -		The spinbox window in which the button was pressed.
# x -		The x-coordinate of the button press.

proc ::tk::spinbox::ButtonDown {w x y} {
    variable ::tk::Priv

    # Get the element that was clicked in.  If we are not directly over
    # the spinbox, default to entry.  This is necessary for spinbox grabs.
    #
    set Priv(element) [$w identify $x $y]
    if {$Priv(element) eq ""} {
	set Priv(element) "entry"
    }

    switch -exact $Priv(element) {
	"buttonup" - "buttondown" {
	    if {[string compare "disabled" [$w cget -state]]} {
		$w selection element $Priv(element)
		set Priv(repeated) 0
		set Priv(relief) [$w cget -$Priv(element)relief]
		after cancel $Priv(afterId)
		set delay [$w cget -repeatdelay]
		if {$delay > 0} {
		    set Priv(afterId) [after $delay \
			    [list ::tk::spinbox::Invoke $w $Priv(element)]]
		}
		if {[info exists Priv(outsideElement)]} {
		    unset Priv(outsideElement)
		}
	    }
	}
	"entry" {
	    set Priv(selectMode) char
	    set Priv(mouseMoved) 0
	    set Priv(pressX) $x
	    $w icursor [::tk::spinbox::ClosestGap $w $x]
	    $w selection from insert
	    if {[string compare "disabled" [$w cget -state]]} {focus $w}
	    $w selection clear
	}
	default {
	    return -code error "unknown spinbox element \"$Priv(element)\""
	}
    }
}

# ::tk::spinbox::ButtonUp --
# This procedure is invoked to handle button-1 releases in spinbox
# widgets.
#
# Arguments:
# w -		The spinbox window in which the button was pressed.
# x -		The x-coordinate of the button press.

proc ::tk::spinbox::ButtonUp {w x y} {
    variable ::tk::Priv

    ::tk::CancelRepeat

    # Priv(relief) may not exist if the ButtonUp is not paired with
    # a preceding ButtonDown
    if {[info exists Priv(element)] && [info exists Priv(relief)] && \
	    [string match "button*" $Priv(element)]} {
	if {[info exists Priv(repeated)] && !$Priv(repeated)} {
	    $w invoke $Priv(element)
	}
	$w configure -$Priv(element)relief $Priv(relief)
	$w selection element none
    }
}

# ::tk::spinbox::MouseSelect --
# This procedure is invoked when dragging out a selection with
# the mouse.  Depending on the selection mode (character, word,
# line) it selects in different-sized units.  This procedure
# ignores mouse motions initially until the mouse has moved from
# one character to another or until there have been multiple clicks.
#
# Arguments:
# w -		The spinbox window in which the button was pressed.
# x -		The x-coordinate of the mouse.
# cursor -	optional place to set cursor.

proc ::tk::spinbox::MouseSelect {w x {cursor {}}} {
    variable ::tk::Priv

    if {[string compare "entry" $Priv(element)]} {
	if {[string compare "none" $Priv(element)] && \
		[string compare "ignore" $cursor]} {
	    $w selection element none
	    $w invoke $Priv(element)
	    $w selection element $Priv(element)
	}
	return
    }
    set cur [::tk::spinbox::ClosestGap $w $x]
    set anchor [$w index anchor]
    if {($cur != $anchor) || (abs($Priv(pressX) - $x) >= 3)} {
	set Priv(mouseMoved) 1
    }
    switch $Priv(selectMode) {
	char {
	    if {$Priv(mouseMoved)} {
		if {$cur < $anchor} {
		    $w selection range $cur $anchor
		} elseif {$cur > $anchor} {
		    $w selection range $anchor $cur
		} else {
		    $w selection clear
		}
	    }
	}
	word {
	    if {$cur < [$w index anchor]} {
		set before [tcl_wordBreakBefore [$w get] $cur]
		set after [tcl_wordBreakAfter [$w get] [expr {$anchor-1}]]
	    } else {
		set before [tcl_wordBreakBefore [$w get] $anchor]
		set after [tcl_wordBreakAfter [$w get] [expr {$cur - 1}]]
	    }
	    if {$before < 0} {
		set before 0
	    }
	    if {$after < 0} {
		set after end
	    }
	    $w selection range $before $after
	}
	line {
	    $w selection range 0 end
	}
    }
    if {[string compare $cursor {}] && [string compare $cursor "ignore"]} {
	catch {$w icursor $cursor}
    }
    update idletasks
}

# ::tk::spinbox::Paste --
# This procedure sets the insertion cursor to the current mouse position,
# pastes the selection there, and sets the focus to the window.
#
# Arguments:
# w -		The spinbox window.
# x -		X position of the mouse.

proc ::tk::spinbox::Paste {w x} {

    $w icursor [::tk::spinbox::ClosestGap $w $x]
    catch {$w insert insert [::tk::GetSelection $w PRIMARY]}
    if {[string equal "disabled" [$w cget -state]]} {focus $w}
}

# ::tk::spinbox::Motion --
# This procedure is invoked when the mouse moves in a spinbox window
# with button 1 down.
#
# Arguments:
# w -		The spinbox window.

proc ::tk::spinbox::Motion {w x y} {
    variable ::tk::Priv

    if {![info exists Priv(element)]} {
	set Priv(element) [$w identify $x $y]
    }

    set Priv(x) $x
    if {[string equal "entry" $Priv(element)]} {
	::tk::spinbox::MouseSelect $w $x ignore
    } elseif {[string compare [$w identify $x $y] $Priv(element)]} {
	if {![info exists Priv(outsideElement)]} {
	    # We've wandered out of the spin button
	    # setting outside element will cause ::tk::spinbox::Invoke to
	    # loop without doing anything
	    set Priv(outsideElement) ""
	    $w selection element none
	}
    } elseif {[info exists Priv(outsideElement)]} {
	unset Priv(outsideElement)
	$w selection element $Priv(element)
    }
}

# ::tk::spinbox::AutoScan --
# This procedure is invoked when the mouse leaves an spinbox window
# with button 1 down.  It scrolls the window left or right,
# depending on where the mouse is, and reschedules itself as an
# "after" command so that the window continues to scroll until the
# mouse moves back into the window or the mouse button is released.
#
# Arguments:
# w -		The spinbox window.

proc ::tk::spinbox::AutoScan {w} {
    variable ::tk::Priv

    set x $Priv(x)
    if {$x >= [winfo width $w]} {
	$w xview scroll 2 units
	::tk::spinbox::MouseSelect $w $x ignore
    } elseif {$x < 0} {
	$w xview scroll -2 units
	::tk::spinbox::MouseSelect $w $x ignore
    }
    set Priv(afterId) [after 50 [list ::tk::spinbox::AutoScan $w]]
}

# ::tk::spinbox::KeySelect --
# This procedure is invoked when stroking out selections using the
# keyboard.  It moves the cursor to a new position, then extends
# the selection to that position.
#
# Arguments:
# w -		The spinbox window.
# new -		A new position for the insertion cursor (the cursor hasn't
#		actually been moved to this position yet).

proc ::tk::spinbox::KeySelect {w new} {
    if {![$w selection present]} {
	$w selection from insert
	$w selection to $new
    } else {
	$w selection adjust $new
    }
    $w icursor $new
}

# ::tk::spinbox::Insert --
# Insert a string into an spinbox at the point of the insertion cursor.
# If there is a selection in the spinbox, and it covers the point of the
# insertion cursor, then delete the selection before inserting.
#
# Arguments:
# w -		The spinbox window in which to insert the string
# s -		The string to insert (usually just a single character)

proc ::tk::spinbox::Insert {w s} {
    if {$s == ""} {
	return
    }
    catch {
	set insert [$w index insert]
	if {([$w index sel.first] <= $insert) \
		&& ([$w index sel.last] >= $insert)} {
	    $w delete sel.first sel.last
	}
    }
    $w insert insert $s
    ::tk::spinbox::SeeInsert $w
}

# ::tk::spinbox::Backspace --
# Backspace over the character just before the insertion cursor.
# If backspacing would move the cursor off the left edge of the
# window, reposition the cursor at about the middle of the window.
#
# Arguments:
# w -		The spinbox window in which to backspace.

proc ::tk::spinbox::Backspace w {
    if {[$w selection present]} {
	$w delete sel.first sel.last
    } else {
	set x [expr {[$w index insert] - 1}]
	if {$x >= 0} {$w delete $x}
	if {[$w index @0] >= [$w index insert]} {
	    set range [$w xview]
	    set left [lindex $range 0]
	    set right [lindex $range 1]
	    $w xview moveto [expr {$left - ($right - $left)/2.0}]
	}
    }
}

# ::tk::spinbox::SeeInsert --
# Make sure that the insertion cursor is visible in the spinbox window.
# If not, adjust the view so that it is.
#
# Arguments:
# w -		The spinbox window.

proc ::tk::spinbox::SeeInsert w {
    set c [$w index insert]
    if {($c < [$w index @0]) || ($c > [$w index @[winfo width $w]])} {
	$w xview $c
    }
}

# ::tk::spinbox::SetCursor -
# Move the insertion cursor to a given position in an spinbox.  Also
# clears the selection, if there is one in the spinbox, and makes sure
# that the insertion cursor is visible.
#
# Arguments:
# w -		The spinbox window.
# pos -		The desired new position for the cursor in the window.

proc ::tk::spinbox::SetCursor {w pos} {
    $w icursor $pos
    $w selection clear
    ::tk::spinbox::SeeInsert $w
}

# ::tk::spinbox::Transpose -
# This procedure implements the "transpose" function for spinbox widgets.
# It tranposes the characters on either side of the insertion cursor,
# unless the cursor is at the end of the line.  In this case it
# transposes the two characters to the left of the cursor.  In either
# case, the cursor ends up to the right of the transposed characters.
#
# Arguments:
# w -		The spinbox window.

proc ::tk::spinbox::Transpose w {
    set i [$w index insert]
    if {$i < [$w index end]} {
	incr i
    }
    set first [expr {$i-2}]
    if {$first < 0} {
	return
    }
    set data [$w get]
    set new [string index $data [expr {$i-1}]][string index $data $first]
    $w delete $first $i
    $w insert insert $new
    ::tk::spinbox::SeeInsert $w
}

# ::tk::spinbox::NextWord --
# Returns the index of the next word position after a given position in the
# spinbox.  The next word is platform dependent and may be either the next
# end-of-word position or the next start-of-word position after the next
# end-of-word position.
#
# Arguments:
# w -		The spinbox window in which the cursor is to move.
# start -	Position at which to start search.

if {[string equal $tcl_platform(platform) "windows"]}  {
    proc ::tk::spinbox::NextWord {w start} {
	set pos [tcl_endOfWord [$w get] [$w index $start]]
	if {$pos >= 0} {
	    set pos [tcl_startOfNextWord [$w get] $pos]
	}
	if {$pos < 0} {
	    return end
	}
	return $pos
    }
} else {
    proc ::tk::spinbox::NextWord {w start} {
	set pos [tcl_endOfWord [$w get] [$w index $start]]
	if {$pos < 0} {
	    return end
	}
	return $pos
    }
}

# ::tk::spinbox::PreviousWord --
#
# Returns the index of the previous word position before a given
# position in the spinbox.
#
# Arguments:
# w -		The spinbox window in which the cursor is to move.
# start -	Position at which to start search.

proc ::tk::spinbox::PreviousWord {w start} {
    set pos [tcl_startOfPreviousWord [$w get] [$w index $start]]
    if {$pos < 0} {
	return 0
    }
    return $pos
}

# ::tk::spinbox::GetSelection --
#
# Returns the selected text of the spinbox.
#
# Arguments:
# w -         The spinbox window from which the text to get

proc ::tk::spinbox::GetSelection {w} {
    return [string range [$w get] [$w index sel.first] \
	    [expr {[$w index sel.last] - 1}]]
}
