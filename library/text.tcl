# text.tcl --
#
# This file defines the default bindings for Tk text widgets and provides
# procedures that help in implementing the bindings.
#
# Copyright (c) 1992-1994 The Regents of the University of California.
# Copyright (c) 1994-1997 Sun Microsystems, Inc.
# Copyright (c) 1998 by Scriptics Corporation.
# Copyright (c) 2015-2017 Gregor Cramer
#
# See the file "license.terms" for information on usage and redistribution
# of this file, and for a DISCLAIMER OF ALL WARRANTIES.
#

##########################################################################
# TODO:
# Currently we cannot use identifier "begin" for very first index, because
# it has still lowest precedence, and this may clash if the application is
# using this identifier for marks. In a later version of this file all
# occurences of "1.0" should be replaced with "begin", as soon as "begin"
# has highest precedence.
##########################################################################

#-------------------------------------------------------------------------
# Elements of ::tk::Priv that are used in this file:
#
# afterId -		If non-null, it means that auto-scanning is underway
#			and it gives the "after" id for the next auto-scan
#			command to be executed.
# char -		Character position on the line;  kept in order
#			to allow moving up or down past short lines while
#			still remembering the desired position.
# mouseMoved -		Non-zero means the mouse has moved a significant
#			amount since the button went down (so, for example,
#			start dragging out a selection).
# prevPos -		Used when moving up or down lines via the keyboard.
#			Keeps track of the previous insert position, so
#			we can distinguish a series of ups and downs, all
#			in a row, from a new up or down.
# selectMode -		The style of selection currently underway:
#			char, word, or line.
# x, y -		Last known mouse coordinates for scanning
#			and auto-scanning.
#
#-------------------------------------------------------------------------

#-------------------------------------------------------------------------
# The code below creates the default class bindings for text widgets.
#-------------------------------------------------------------------------

# Standard Motif bindings:

bind Text <1> {
    tk::TextButton1 %W %x %y
    %W tag remove sel 1.0 end
}
bind Text <B1-Motion> {
    set tk::Priv(x) %x
    set tk::Priv(y) %y
    tk::TextSelectTo %W %x %y
}
bind Text <Double-1> {
    set tk::Priv(selectMode) word
    tk::TextSelectTo %W %x %y
    catch {%W mark set insert sel.first}
}
bind Text <Triple-1> {
    set tk::Priv(selectMode) line
    tk::TextSelectTo %W %x %y
    catch {%W mark set insert sel.first}
}
bind Text <Shift-1> {
    tk::TextResetAnchor %W @%x,%y
    set tk::Priv(selectMode) char
    tk::TextSelectTo %W %x %y
}
bind Text <Double-Shift-1>	{
    set tk::Priv(selectMode) word
    tk::TextSelectTo %W %x %y 1
}
bind Text <Triple-Shift-1>	{
    set tk::Priv(selectMode) line
    tk::TextSelectTo %W %x %y
}
bind Text <B1-Leave> {
    set tk::Priv(x) %x
    set tk::Priv(y) %y
    tk::TextAutoScan %W
}
bind Text <B1-Enter> {
    tk::CancelRepeat
}
bind Text <ButtonRelease-1> {
    tk::CancelRepeat
}
bind Text <Control-1> {
    %W mark set insert @%x,%y
    # An operation that moves the insert mark without making it
    # one end of the selection must insert an autoseparator
    if {[%W cget -autoseparators]} {
	%W edit separator
    }
}
# stop an accidental double click triggering <Double-Button-1>
bind Text <Double-Control-1> { # nothing }
# stop an accidental movement triggering <B1-Motion>
bind Text <Control-B1-Motion> { # nothing }
bind Text <<PrevChar>> {
    tk::TextSetCursor %W insert-1displaychars
}
bind Text <<NextChar>> {
    tk::TextSetCursor %W insert+1displaychars
}
bind Text <<PrevLine>> {
    tk::TextSetCursor %W [tk::TextUpDownLine %W -1]
}
bind Text <<NextLine>> {
    tk::TextSetCursor %W [tk::TextUpDownLine %W 1]
}
bind Text <<SelectPrevChar>> {
    tk::TextKeySelect %W [%W index {insert - 1displaychars}]
}
bind Text <<SelectNextChar>> {
    tk::TextKeySelect %W [%W index {insert + 1displaychars}]
}
bind Text <<SelectPrevLine>> {
    tk::TextKeySelect %W [tk::TextUpDownLine %W -1 yes]
}
bind Text <<SelectNextLine>> {
    tk::TextKeySelect %W [tk::TextUpDownLine %W 1 yes]
}
bind Text <<PrevWord>> {
    tk::TextSetCursor %W [tk::TextPrevPos %W insert tcl_startOfPreviousWord]
}
bind Text <<NextWord>> {
    tk::TextSetCursor %W [tk::TextNextWord %W insert]
}
bind Text <<PrevPara>> {
    tk::TextSetCursor %W [tk::TextPrevPara %W insert]
}
bind Text <<NextPara>> {
    tk::TextSetCursor %W [tk::TextNextPara %W insert]
}
bind Text <<SelectPrevWord>> {
    tk::TextKeySelect %W [tk::TextPrevPos %W insert tcl_startOfPreviousWord]
}
bind Text <<SelectNextWord>> {
    tk::TextKeySelect %W [tk::TextNextWord %W insert]
}
bind Text <<SelectPrevPara>> {
    tk::TextKeySelect %W [tk::TextPrevPara %W insert]
}
bind Text <<SelectNextPara>> {
    tk::TextKeySelect %W [tk::TextNextPara %W insert]
}
bind Text <Prior> {
    tk::TextSetCursor %W [tk::TextScrollPages %W -1]
}
bind Text <Shift-Prior> {
    tk::TextKeySelect %W [tk::TextScrollPages %W -1]
}
bind Text <Next> {
    tk::TextSetCursor %W [tk::TextScrollPages %W 1]
}
bind Text <Shift-Next> {
    tk::TextKeySelect %W [tk::TextScrollPages %W 1]
}
bind Text <Control-Prior> {
    %W xview scroll -1 page
}
bind Text <Control-Next> {
    %W xview scroll 1 page
}

bind Text <<LineStart>> {
    tk::TextSetCursor %W {insert display linestart}
}
bind Text <<SelectLineStart>> {
    tk::TextKeySelect %W {insert display linestart}
}
bind Text <<LineEnd>> {
    tk::TextSetCursor %W {insert display lineend}
}
bind Text <<SelectLineEnd>> {
    tk::TextKeySelect %W {insert display lineend}
}
bind Text <Control-Home> {
    tk::TextSetCursor %W 1.0
}
bind Text <Control-Shift-Home> {
    tk::TextKeySelect %W 1.0
}
bind Text <Control-End> {
    tk::TextSetCursor %W {end - 1 indices}
}
bind Text <Control-Shift-End> {
    tk::TextKeySelect %W {end - 1 indices}
}

bind Text <Tab> {
    if {[%W cget -state] eq "normal"} {
	tk::TextInsert %W \t
	focus %W
	break
    }
}
bind Text <Shift-Tab> {
    # Needed only to keep <Tab> binding from triggering; doesn't
    # have to actually do anything.
    break
}
bind Text <Control-Tab> {
    focus [tk_focusNext %W]
}
bind Text <Control-Shift-Tab> {
    focus [tk_focusPrev %W]
}
bind Text <Control-i> {
    tk::TextInsert %W \t
}
bind Text <Return> {
    if {[%W cget -state] eq "normal"} {
	tk::TextInsert %W \n
	if {[%W cget -autoseparators]} {
	    %W edit separator
	}
    }
}
bind Text <Delete> {
    if {[%W cget -state] eq "normal"} {
	if {[tk::TextCursorInSelection %W]} {
	    tk::TextDelete %W sel.first sel.last
	} else {
	    if {[%W compare end != insert+1i]} {
		%W delete insert
	    }
	    %W see insert
	}
    }
}
bind Text <BackSpace> {
    if {[%W cget -state] eq "normal"} {
	if {[tk::TextCursorInSelection %W]} {
	    tk::TextDelete %W sel.first sel.last
	} else {
	    if {[%W compare insert != 1.0]} {
		# ensure that this operation is triggering "watch"
		%W mark set insert insert-1i
		%W delete insert
	    }
	    %W see insert
	}
    }
}

bind Text <Control-space> {
    %W mark set [tk::TextAnchor %W] insert
}
bind Text <Select> {
    %W mark set [tk::TextAnchor %W] insert
}
bind Text <Control-Shift-space> {
    set tk::Priv(selectMode) char
    tk::TextKeyExtend %W insert
}
bind Text <Shift-Select> {
    set tk::Priv(selectMode) char
    tk::TextKeyExtend %W insert
}
bind Text <<SelectAll>> {
    %W tag add sel 1.0 end
}
bind Text <<SelectNone>> {
    %W tag remove sel 1.0 end
    # An operation that clears the selection must insert an autoseparator,
    # because the selection operation may have moved the insert mark.
    if {[%W cget -autoseparators]} {
	%W edit separator
    }
}
bind Text <<Cut>> {
    tk_textCut %W
}
bind Text <<Copy>> {
    tk_textCopy %W
}
bind Text <<Paste>> {
    tk_textPaste %W
}
bind Text <<Clear>> {
    if {[%W cget -state] eq "normal"} {
	# Make <<Clear>> an atomic operation on the Undo stack,
	# i.e. separate it from other delete operations on either side
	if {[%W cget -autoseparators]} {
	    %W edit separator
	}
	catch { tk::TextDelete %W sel.first sel.last }
	if {[%W cget -autoseparators]} {
	    %W edit separator
	}
    }
}
bind Text <<PasteSelection>> {
    if {$tk_strictMotif || ![info exists tk::Priv(mouseMoved)] || !$tk::Priv(mouseMoved)} {
	tk::TextPasteSelection %W %x %y
    }
}
bind Text <Insert> {
    if {[%W cget -state] eq "normal"} {
	catch {tk::TextInsert %W [::tk::GetSelection %W PRIMARY]}
    }
}
bind Text <KeyPress> {
    tk::TextInsert %W %A
}

# Ignore all Alt, Meta, and Control keypresses unless explicitly bound.
# Otherwise, if a widget binding for one of these is defined, the
# <KeyPress> class binding will also fire and insert the character,
# which is wrong.  Ditto for <Escape>.

bind Text <Alt-KeyPress> {# nothing }
bind Text <Meta-KeyPress> {# nothing}
bind Text <Control-KeyPress> {# nothing}
bind Text <Escape> {# nothing}
bind Text <KP_Enter> {# nothing}
if {[tk windowingsystem] eq "aqua"} {
    bind Text <Command-KeyPress> {# nothing}
}

# Additional emacs-like bindings:

bind Text <Control-d> {
    if {[%W cget -state] eq "normal" && !$tk_strictMotif && [%W compare end != insert+1i]} {
	%W delete insert
    }
}
bind Text <Control-k> {
    if {[%W cget -state] eq "normal" && !$tk_strictMotif && [%W compare end != insert+1i]} {
	if {[%W compare insert == {insert lineend}]} {
	    %W delete insert
	} else {
	    %W delete insert {insert lineend}
	}
    }
}
bind Text <Control-o> {
    if {[%W cget -state] eq "normal" && !$tk_strictMotif} {
	%W insert insert \n
	%W mark set insert insert-1i
    }
}
bind Text <Control-t> {
    if {!$tk_strictMotif} {
	tk::TextTranspose %W
    }
}

bind Text <<Undo>> {
    if {[%W cget -state] eq "normal"} {
	# An Undo operation may remove the separator at the top of the Undo stack.
	# Then the item at the top of the stack gets merged with the subsequent changes.
	# Place separators before and after Undo to prevent this.
	if {[%W cget -autoseparators]} {
	    %W edit separator
	}
	catch { %W edit undo }
	if {[%W cget -autoseparators]} {
	    %W edit separator
	}
    }
}

bind Text <<Redo>> {
    if {[%W cget -state] eq "normal"} {
	catch { %W edit redo }
    }
}

bind Text <Meta-b> {
    if {!$tk_strictMotif} {
	tk::TextSetCursor %W [tk::TextPrevPos %W insert tcl_startOfPreviousWord]
    }
}
bind Text <Meta-d> {
    if {!$tk_strictMotif && [%W compare end != insert+1i]} {
	%W delete insert [tk::TextNextWord %W insert]
    }
}
bind Text <Meta-f> {
    if {!$tk_strictMotif} {
	tk::TextSetCursor %W [tk::TextNextWord %W insert]
    }
}
bind Text <Meta-less> {
    if {!$tk_strictMotif} {
	tk::TextSetCursor %W 1.0
    }
}
bind Text <Meta-greater> {
    if {!$tk_strictMotif} {
	tk::TextSetCursor %W end-1i
    }
}
bind Text <Meta-BackSpace> {
    if {[%W cget -state] eq "normal" && !$tk_strictMotif} {
	tk::TextDelete %W [tk::TextPrevPos %W insert tcl_startOfPreviousWord] insert
    }
}
bind Text <Meta-Delete> {
    if {[%W cget -state] eq "normal" && !$tk_strictMotif} {
	tk::TextDelete %W [tk::TextPrevPos %W insert tcl_startOfPreviousWord] insert
    }
}

# Macintosh only bindings:

if {[tk windowingsystem] eq "aqua"} {
   bind Text <Control-v> {
       tk::TextScrollPages %W 1
   }

# End of Mac only bindings
}

# A few additional bindings of my own.

bind Text <Control-h> {
    if {[%W cget -state] eq "normal" && !$tk_strictMotif && [%W compare insert != 1.0]} {
	# ensure that this operation is triggering "watch"
	%W mark set insert insert-1i
	%W delete insert
	%W see insert
    }
}
bind Text <2> {
    if {!$tk_strictMotif} {
	tk::TextScanMark %W %x %y
    }
}
bind Text <B2-Motion> {
    if {!$tk_strictMotif} {
	tk::TextScanDrag %W %x %y
    }
}
set ::tk::Priv(prevPos) {}

# The MouseWheel events:
# We must be careful not to round -ve values of %D down to zero.
 
if {[tk windowingsystem] eq "aqua"} {
    bind Text <MouseWheel> {
	%W yview scroll [expr {-15 * (%D)}] pixels
    }
    bind Text <Option-MouseWheel> {
	%W yview scroll [expr {-150 * (%D)}] pixels
    }
    bind Text <Shift-MouseWheel> {
	%W xview scroll [expr {-15 * (%D)}] pixels
    }
    bind Text <Shift-Option-MouseWheel> {
	%W xview scroll [expr {-150 * (%D)}] pixels
    }
} else {
    # We must make sure that positive and negative movements are rounded
    # equally to integers, avoiding the problem that
    #     (int)1/3 = 0,
    # but
    #     (int)-1/3 = -1
    # The following code ensure equal +/- behaviour.
    bind Text <MouseWheel> {
	%W yview scroll [expr {%D >= 0 ? -%D/3 : (2-%D)/3}] pixels
    }
    bind Text <Shift-MouseWheel> {
	%W xview scroll [expr {%D >= 0 ? -%D/3 : (2-%D)/3}] pixels
    }
}

if {"x11" eq [tk windowingsystem]} {
    # Support for mousewheels on Linux/Unix commonly comes through mapping
    # the wheel to the extended buttons.  If you have a mousewheel, find
    # Linux configuration info at:
    #	http://www.inria.fr/koala/colas/mouse-wheel-scroll/
    bind Text <4> {
	if {!$tk_strictMotif} { %W yview scroll -50 pixels }
    }
    bind Text <5> {
	if {!$tk_strictMotif} { %W yview scroll +50 pixels }
    }
    bind Text <Shift-4> {
	if {!$tk_strictMotif} { %W xview scroll -50 pixels }
    }
    bind Text <Shift-5> {
	if {!$tk_strictMotif} { %W xview scroll +50 pixels }
    }
}

# ::tk::TextCursorPos --
# Given x and y coordinates, this procedure computes the "cursor"
# position, and returns the index of the character at this position.
#
# Arguments:
# w -		The text window.
# x -		X-coordinate within the window.
# y -		Y-coordinate within the window.

proc ::tk::TextCursorPos {w x y} {
    if {[$w cget -blockcursor]} {
	# If we have a block cursor, then use the actual x-position
	# for cursor position.
	return [$w index @$x,$y]
    }
    return [TextClosestGap $w $x $y]
}

# ::tk::TextClosestGap --
# Given x and y coordinates, this procedure finds the closest boundary
# between characters to the given coordinates and returns the index
# of the character just after the boundary.
#
# Arguments:
# w -		The text window.
# x -		X-coordinate within the window.
# y -		Y-coordinate within the window.

proc ::tk::TextClosestGap {w x y} {
    set pos [$w index @$x,$y]
    set bbox [$w bbox $pos]
    if {[llength $bbox] == 0} {
    	return $pos
    }
    if {($x - [lindex $bbox 0]) < ([lindex $bbox 2]/2)} {
    	return $pos
    }
    $w index "$pos + 1i"
}

# ::tk::TextButton1 --
# This procedure is invoked to handle button-1 presses in text
# widgets.  It moves the insertion cursor, sets the selection anchor,
# and claims the input focus.
#
# Arguments:
# w -		The text window in which the button was pressed.
# x -		The x-coordinate of the button press.
# y -		The x-coordinate of the button press.

proc ::tk::TextButton1 {w x y} {
    variable Priv
    # Catch the very special case with dead peers.
    if {![$w isdead]} {
	set Priv(selectMode) char
	set Priv(mouseMoved) 0
	set Priv(pressX) $x
	set pos [TextCursorPos $w $x $y]
	set thisLineNo [$w lineno @last,$y]
	if {[$w lineno $pos] ne $thisLineNo} {
	    # The button has been pressed at an x position after last character.
	    # In this case [$w index @$x,$y] is returning the start of next line,
	    # but we want the end of this line.
	    set pos "$thisLineNo.end"
	}
	$w mark set insert $pos
	if {[$w cget -blockcursor]} {
	    set anchor [TextClosestGap $w $x $y]
	} else {
	    # this is already the closest gap
	    set anchor insert
	}
	# Set the anchor mark's gravity depending on the click position
	# relative to the gap.
	set bbox [$w bbox $anchor]
	set gravity [expr {$x > [lindex $bbox 0] ? "right" : "left"}]
	$w mark set [TextAnchor $w] $anchor $gravity
	if {[$w cget -state] eq "normal" && [$w cget -autoseparators]} {
	    $w edit separator
	}
    }

    # Allow focus in any case on Windows, because that will let the
    # selection be displayed even for state disabled text widgets.
    if {[tk windowingsystem] eq "win32" || [$w cget -state] eq "normal"} {
	focus $w
    }
}

# ::tk::TextSelectTo --
# This procedure is invoked to extend the selection, typically when
# dragging it with the mouse.  Depending on the selection mode (character,
# word, line) it selects in different-sized units.  This procedure
# ignores mouse motions initially until the mouse has moved from
# one character to another or until there have been multiple clicks.
#
# Note that the 'anchor' is implemented programmatically using
# a text widget mark, and uses a name that will be unique for each
# text widget (even when there are multiple peers).
#
# Arguments:
# w -		The text window in which the button was pressed.
# x -		Mouse x position.
# y - 		Mouse y position.

proc ::tk::TextAnchor {w} {
    variable Priv

    if {![info exists Priv(textanchor,$w)]} {
	# This gives us a private mark, not visible with
	# "mark names|next|previous|..".
	set Priv(textanchor,$w) [$w mark generate]
	# The Tk library still has a big weakness: it's not possible to
	# bind variables to a widget, so we use a private command for this
	# binding; this means that the variable will be unset automatically
	# when the widget will be destroyed. This is the only proper way to
	# handle unique identifiers.
	$w tk_bindvar [namespace current]::Priv(textanchor,$w)
    }
    return $Priv(textanchor,$w)
}

proc ::tk::TextSelectTo {w x y {extend 0}} {
    variable Priv
    if {[$w isdead]} {
	# Catch the very special case with dead peers.
	return
    }
    set anchorname [TextAnchor $w]
    set cur [TextCursorPos $w $x $y]
    if {![$w mark exists $anchorname]} {
	$w mark set $anchorname $cur
    }
    set anchor [$w index $anchorname]
    if {[$w compare $cur != $anchor] || (abs($Priv(pressX) - $x) >= 3)} {
	set Priv(mouseMoved) 1
    }
    switch -- $Priv(selectMode) {
	char {
	    if {[$w compare $cur < $anchorname]} {
		set first $cur
		set last $anchorname
	    } else {
		set first $anchorname
		set last $cur
	    }
	}
	word {
	    set first [$w index @$x,$y]
	    set isEmbedded [expr {[string length [$w get $first]] == 0}]
	    if {$isEmbedded} {
		# Don't extend the range if we have an embedded item at this position
		set last "$first+1i"
	    } else {
		# Set initial range based only on the anchor (1 char min width)
		if {[$w mark gravity $anchorname] eq "right"} {
		    set first $anchorname
		    set last "$anchorname + 1i"
		} else {
		    set first "$anchorname - 1i"
		    set last $anchorname
		}
		# Extend range (if necessary) based on the current point
		if {[$w compare $cur < $first]} {
		    set first $cur
		} elseif {[$w compare $cur > $last]} {
		    set last $cur
		}

		# Now find word boundaries
		set first [TextPrevPos $w "$first + 1i" tcl_wordBreakBefore]
		set last [TextNextPos $w "$last - 1i" tcl_wordBreakAfter]
	    }
	}
	line {
	    # Set initial range based only on the anchor
	    set first "$anchorname linestart"
	    set last "$anchorname lineend"

	    # Extend range (if necessary) based on the current point
	    if {[$w compare $cur < $first]} {
		set first "$cur linestart"
	    } elseif {[$w compare $cur > $last]} {
		set last "$cur lineend"
	    }
	    set first [$w index $first]
	    set last [$w index "$last + 1i"]
	}
    }
    if {$Priv(mouseMoved) || ($Priv(selectMode) ne "char")} {
	$w mark set insert $cur
	$w tag remove sel 1.0 $first
	$w tag add sel $first $last
	$w tag remove sel $last end
	update idletasks
    }
}

# ::tk::TextKeyExtend --
# This procedure handles extending the selection from the keyboard,
# where the point to extend to is really the boundary between two
# characters rather than a particular character.
#
# Arguments:
# w -		The text window.
# index -	The point to which the selection is to be extended.

proc ::tk::TextKeyExtend {w index} {
    set anchorname [TextAnchor $w]
    set cur [$w index $index]
    if {![$w mark exists $anchorname]} {
	$w mark set $anchorname $cur left
    }
    set anchor [$w index $anchorname]
    if {[$w compare $cur < $anchorname]} {
	set first $cur
	set last $anchorname
    } else {
	set first $anchorname
	set last $cur
    }
    $w tag remove sel 1.0 $first
    $w tag add sel $first $last
    $w tag remove sel $last end
}

# ::tk::TextPasteSelection --
# This procedure sets the insertion cursor to the mouse position,
# inserts the selection, and sets the focus to the window.
#
# Arguments:
# w -		The text window.
# x, y - 	Position of the mouse.

proc ::tk::TextPasteSelection {w x y} {
    if {[$w cget -state] eq "normal"} {
	$w mark set insert [TextCursorPos $w $x $y]
	TextInsertSelection $w PRIMARY
    }
    if {[$w cget -state] eq "normal"} {
	focus $w
    }
}

# ::tk::TextAutoScan --
# This procedure is invoked when the mouse leaves a text window
# with button 1 down.  It scrolls the window up, down, left, or right,
# depending on where the mouse is (this information was saved in
# ::tk::Priv(x) and ::tk::Priv(y)), and reschedules itself as an "after"
# command so that the window continues to scroll until the mouse
# moves back into the window or the mouse button is released.
#
# Arguments:
# w -		The text window.

proc ::tk::TextAutoScan {w} {
    variable Priv
    if {![winfo exists $w]} {
	return
    }
    if {$Priv(y) >= [winfo height $w]} {
	$w yview scroll [expr {1 + $Priv(y) - [winfo height $w]}] pixels
    } elseif {$Priv(y) < 0} {
	$w yview scroll [expr {-1 + $Priv(y)}] pixels
    } elseif {$Priv(x) >= [winfo width $w]} {
	$w xview scroll 2 units
    } elseif {$Priv(x) < 0} {
	$w xview scroll -2 units
    } else {
	return
    }
    TextSelectTo $w $Priv(x) $Priv(y)
    set Priv(afterId) [after 50 [list ::tk::TextAutoScan $w]]
}

# ::tk::TextSetCursor
# Move the insertion cursor to a given position in a text.  Also
# clears the selection, if there is one in the text, and makes sure
# that the insertion cursor is visible.  Also, don't let the insertion
# cursor appear on the dummy last line of the text.
#
# Arguments:
# w -		The text window.
# pos -		The desired new position for the cursor in the window.

proc ::tk::TextSetCursor {w pos} {
    if {[$w compare $pos == end]} {
	set pos {end - 1i}
    }
    $w mark set insert $pos
    $w tag remove sel 1.0 end
    $w see insert
    if {[$w cget -autoseparators]} {
	$w edit separator
    }
}

# ::tk::TextKeySelect
# This procedure is invoked when stroking out selections using the
# keyboard.  It moves the cursor to a new position, then extends
# the selection to that position.
#
# Arguments:
# w -		The text window.
# new -		A new position for the insertion cursor (the cursor hasn't
#		actually been moved to this position yet).

proc ::tk::TextKeySelect {w new} {
    if {[$w isdead]} {
	# Catch the very special case with dead peers.
	return
    }
    set anchorname [TextAnchor $w]
    if {[llength [$w tag nextrange sel 1.0 end]] == 0} {
	if {[$w compare $new < insert]} {
	    $w tag add sel $new insert
	} else {
	    $w tag add sel insert $new
	}
	$w mark set $anchorname insert
    } else {
	if {[$w compare $new < $anchorname]} {
	    set first $new
	    set last $anchorname
	} else {
	    set first $anchorname
	    set last $new
	}
	$w tag remove sel 1.0 $first
	$w tag add sel $first $last
	$w tag remove sel $last end
    }
    $w mark set insert $new
    $w see insert
    update idletasks
}

# ::tk::TextResetAnchor --
# Set the selection anchor to whichever end is farthest from the
# index argument.  One special trick: if the selection has two or
# fewer characters, just leave the anchor where it is.  In this
# case it doesn't matter which point gets chosen for the anchor,
# and for the things like Shift-Left and Shift-Right this produces
# better behavior when the cursor moves back and forth across the
# anchor.
#
# Arguments:
# w -		The text widget.
# index -	Position at which mouse button was pressed, which determines
#		which end of selection should be used as anchor point.

proc ::tk::TextResetAnchor {w index} {
    if {[llength [$w tag ranges sel]] == 0} {
	# Don't move the anchor if there is no selection now; this
	# makes the widget behave "correctly" when the user clicks
	# once, then shift-clicks somewhere -- ie, the area between
	# the two clicks will be selected. [Bug: 5929].
	return
    }
    set anchorname [TextAnchor $w]
    set a [$w index $index]
    set b [$w index sel.first]
    set c [$w index sel.last]
    if {[$w compare $a < $b]} {
	$w mark set $anchorname sel.last
	return
    }
    if {[$w compare $a > $c]} {
	$w mark set $anchorname sel.first
	return
    }
    scan $a "%d.%d" lineA chA
    scan $b "%d.%d" lineB chB
    scan $c "%d.%d" lineC chC
    if {$lineB < $lineC + 2} {
	set total [string length [$w get $b $c]]
	if {$total <= 2} {
	    return
	}
	if {[string length [$w get $b $a]] < ($total/2)} {
	    $w mark set $anchorname sel.last
	} else {
	    $w mark set $anchorname sel.first
	}
	return
    }
    if {$lineA - $lineB < $lineC - $lineA} {
	$w mark set $anchorname sel.last
    } else {
	$w mark set $anchorname sel.first
    }
}

# ::tk::TextCursorInSelection --
# Check whether the selection exists and contains the insertion cursor. Note
# that it assumes that the selection is contiguous.
#
# Arguments:
# w -		The text widget whose selection is to be checked

proc ::tk::TextCursorInSelection {w} {
    expr {[llength [$w tag ranges sel]]
	&& [$w compare sel.first <= insert]
	&& [$w compare sel.last >= insert]
    }
}

# ::tk::TextInsert --
# Insert a string into a text at the point of the insertion cursor.
# If there is a selection in the text, and it covers the point of the
# insertion cursor, then delete the selection before inserting.
#
# Arguments:
# w -		The text window in which to insert the string
# s -		The string to insert (usually just a single character)

proc ::tk::TextInsert {w s} {
    if {[string length $s] == 0 || [$w cget -state] ne "normal"} {
	return
    }
    if {[TextCursorInSelection $w]} {
	if {[$w cget -autoseparators]} {
	    $w edit separator
	}
	# ensure that this operation is triggering "watch"
	$w mark set insert sel.first
	$w replace insert sel.last $s
    } else {
	$w insert insert $s
    }
    $w see insert
}

# ::tk::TextUpDownLine --
# Returns the index of the character one display line above or below the
# insertion cursor.  There are two tricky things here.  First, we want to
# maintain the original x position across repeated operations, even though
# some lines that will get passed through don't have enough characters to
# cover the original column.  Second, don't try to scroll past the
# beginning or end of the text if we don't select.
#
# Arguments:
# w -		The text window in which the cursor is to move.
# n -		The number of display lines to move: -1 for up one line,
#		+1 for down one line.
# sel		Boolean value whether we are selecting text.

proc ::tk::TextUpDownLine {w n {sel no}} {
    variable Priv

    set i [$w index insert]
    if {$Priv(prevPos) ne $i} {
	set Priv(textPosOrig) $i
    }
    set lines [$w count -displaylines $Priv(textPosOrig) $i]
    set new [$w index "$Priv(textPosOrig) + [expr {$lines + $n}] displaylines"]
    if {!$sel && ([$w compare $new == end] || [$w compare $new == "insert display linestart"])} {
	set new $i
    }
    set Priv(prevPos) $new
    return $new
}

# ::tk::TextPrevPara --
# Returns the index of the beginning of the paragraph just before a given
# position in the text (the beginning of a paragraph is the first non-blank
# character after a blank line).
#
# Arguments:
# w -		The text window in which the cursor is to move.
# pos -		Position at which to start search.

proc ::tk::TextPrevPara {w pos} {
    set pos [$w index "$pos linestart"]
    while {1} {
	set newPos [$w index "$pos - 1 line"]
	if {([$w get $newPos] eq "\n" && ([$w get $pos] ne "\n")) || [$w compare $pos == 1.0]} {
	    if {[regexp -indices -- {^[ \t]+(.)} [$w get $pos "$pos lineend"] -> index]} {
		set pos [$w index "$pos + [lindex $index 0] chars"]
	    }
	    if {[$w compare $pos != insert] || [$w compare [$w index "$pos linestart"] == 1.0]} {
		return $pos
	    }
	}
	set pos $newPos
    }
}

# ::tk::TextNextPara --
# Returns the index of the beginning of the paragraph just after a given
# position in the text (the beginning of a paragraph is the first non-blank
# character after a blank line).
#
# Arguments:
# w -		The text window in which the cursor is to move.
# start -	Position at which to start search.

proc ::tk::TextNextPara {w start} {
    set pos [$w index "$start linestart + 1 line"]
    while {[$w get $pos] ne "\n"} {
	if {[$w compare $pos == end]} {
	    return [$w index "end - 1i"]
	}
	set pos [$w index "$pos + 1 line"]
    }
    while {[$w get $pos] eq "\n"} {
	set pos [$w index "$pos + 1 line"]
	if {[$w compare $pos == end]} {
	    return [$w index "end - 1i"]
	}
    }
    if {[regexp -indices -- {^[ \t]+(.)} [$w get $pos "$pos lineend"] -> index]} {
	return [$w index "$pos + [lindex $index 0] chars"]
    }
    return $pos
}

# ::tk::TextScrollPages --
# This is a utility procedure used in bindings for moving up and down
# pages and possibly extending the selection along the way.  It scrolls
# the view in the widget by the number of pages, and it returns the
# index of the character that is at the same position in the new view
# as the insertion cursor used to be in the old view.
#
# Arguments:
# w -		The text window in which the cursor is to move.
# count -	Number of pages forward to scroll;  may be negative
#		to scroll backwards.

proc ::tk::TextScrollPages {w count} {
    if {$count > 0} {
	if {[llength [$w dlineinfo end-1c]] && [llength [$w bbox -discardpartial @first,last]]} {
	    # First character on last display line of very last line is entirely visible,
	    # nothing to do.
	    return insert
	}
    } else {
	if {[llength [$w dlineinfo 1.0]] && [llength [$w bbox -discardpartial @first,0]]} {
	    # First character on first display line of very first line is entirely visible,
	    # nothing to do.
	    return insert
	}
    }
    set bbox [$w bbox insert]
    $w yview scroll $count pages
    if {[llength $bbox] == 0} {
	set newPos [$w index @[expr {[winfo height $w]/2}],0]
    } else {
	set newPos [$w index @[lindex $bbox 0],[lindex $bbox 1]]
    }
    if {[$w compare insert == $newPos]} {
	# This may happen if a character is spanning the entire view,
	# ensure that at least one line will change.
	set idx [expr {$count > 0 ? "insert +1 displayline" : "insert -1 displayline"}]
	set newPos [$w index $idx]
    }
    return $newPos
}

# ::tk::TextTranspose --
# This procedure implements the "transpose" function for text widgets.
# It tranposes the characters on either side of the insertion cursor,
# unless the cursor is at the end of the line.  In this case it
# transposes the two characters to the left of the cursor.  In either
# case, the cursor ends up to the right of the transposed characters.
#
# Arguments:
# w -		Text window in which to transpose.

proc ::tk::TextTranspose w {
    if {[$w cget -state] ne "normal" || [$w compare insert == 1.0]} {
	return
    }
    set pos insert
    if {[$w compare insert != "insert lineend"]} {
	append pos +1i
    }
    set pos [$w index $pos]
    # ensure that this operation is triggering "watch"
    set insPos [$w index insert]
    $w mark set insert ${pos}-2c
    set new [$w get insert+1i][$w get insert]
    $w replace insert $pos $new
    $w mark set insert $insPos
    $w see insert
}

# ::tk_textCopy --
# This procedure copies the selection from a text widget into the
# clipboard.
#
# Arguments:
# w -		Name of a text widget.

proc ::tk_textCopy w {
    if {![catch {set data [$w get sel.first sel.last]}]} {
	clipboard clear -displayof $w
	clipboard append -displayof $w $data
    }
}

# ::tk_textCut --
# This procedure copies the selection from a text widget into the
# clipboard, then deletes the selection (if it exists in the given
# widget).
#
# Arguments:
# w -		Name of a text widget.

proc ::tk_textCut w {
    if {![catch {set data [$w get sel.first sel.last]}]} {
	# make <<Cut>> an atomic operation on the Undo stack,
	# i.e. separate it from other delete operations on either side
	if {[$w cget -autoseparators]} {
	    $w edit separator
	}
	clipboard clear -displayof $w
	clipboard append -displayof $w $data
	if {[$w cget -state] eq "normal"} {
	    ::tk::TextDelete $w sel.first sel.last
	}
	if {[$w cget -autoseparators]} {
	    $w edit separator
	}
    }
}

# ::tk_textPaste --
# This procedure pastes the contents of the clipboard to the insertion
# point in a text widget.
#
# Arguments:
# w -		Name of a text widget.

proc ::tk_textPaste w {
    if {[$w cget -state] eq "normal"} {
	::tk::TextInsertSelection $w CLIPBOARD
    }
}

# ::tk::TextNextWord --
# Returns the index of the next word position after a given position in the
# text.  The next word is platform dependent and may be either the next
# end-of-word position or the next start-of-word position after the next
# end-of-word position.
#
# Arguments:
# w -		The text window in which the cursor is to move.
# start -	Position at which to start search.

if {[tk windowingsystem] eq "win32"}  {
    proc ::tk::TextNextWord {w start} {
	TextNextPos $w [TextNextPos $w $start tcl_endOfWord] tcl_startOfNextWord
    }
} else {
    proc ::tk::TextNextWord {w start} {
	TextNextPos $w $start tcl_endOfWord
    }
}

# ::tk::TextNextPos --
# Returns the index of the next position after the given starting
# position in the text as computed by a specified function.
#
# Arguments:
# w -		The text window in which the cursor is to move.
# start -	Position at which to start search.
# op -		Function to use to find next position.

proc ::tk::TextNextPos {w start op} {
    set text ""
    set cur $start
    while {[$w compare $cur < end]} {
	set end [$w index "$cur lineend + 1i"]
	append text [$w get -displaychars $cur $end]
	set pos [$op $text 0]
	if {$pos >= 0} {
	    return [$w index "$start + $pos display chars"]
	}
	set cur $end
    }
    return end
}

# ::tk::TextPrevPos --
# Returns the index of the previous position before the given starting
# position in the text as computed by a specified function.
#
# Arguments:
# w -		The text window in which the cursor is to move.
# start -	Position at which to start search.
# op -		Function to use to find next position.

proc ::tk::TextPrevPos {w start op} {
    set text ""
    set succ ""
    set cur $start
    while {[$w compare $cur > 1.0]} {
	append text [$w get -displaychars "$cur linestart - 1i" $cur] $succ
	set pos [$op $text end]
	if {$pos >= 0} {
	    return [$w index "$cur linestart - 1i + $pos display chars"]
	}
	set cur [$w index "$cur linestart - 1i"]
	set succ $text
    }
    return 1.0
}

# ::tk::TextScanMark --
#
# Marks the start of a possible scan drag operation
#
# Arguments:
# w -	The text window from which the text to get
# x -	x location on screen
# y -	y location on screen

proc ::tk::TextScanMark {w x y} {
    variable Priv
    $w scan mark $x $y
    set Priv(x) $x
    set Priv(y) $y
    set Priv(mouseMoved) 0
}

# ::tk::TextScanDrag --
#
# Marks the start of a possible scan drag operation
#
# Arguments:
# w -	The text window from which the text to get
# x -	x location on screen
# y -	y location on screen

proc ::tk::TextScanDrag {w x y} {
    variable Priv
    # Make sure these exist, as some weird situations can trigger the
    # motion binding without the initial press.  [Bug #220269]
    if {![info exists Priv(x)]} {
	set Priv(x) $x
    }
    if {![info exists Priv(y)]} {
	set Priv(y) $y
    }
    if {$x != $Priv(x) || $y != $Priv(y)} {
	set Priv(mouseMoved) 1
    }
    if {[info exists Priv(mouseMoved)] && $Priv(mouseMoved)} {
	$w scan dragto $x $y
    }
}

# ::tk::TextDelete --
#
# Delete the characters in given range.
# Ensure that "watch" will be triggered, and consider
# that "insert" may be involved in the given range.
# This implementation avoids unnecessary mappings of indices.

proc ::tk::TextDelete {w start end} {
    # Remember old positions, use temporary marks ('mark generate'),
    # take into account that $end may refer "insert" mark.
    $w mark set [set insPos [$w mark generate]] insert
    $w mark set [set endPos [$w mark generate]] $end
    $w mark set insert $start
    $w delete insert $endPos
    $w mark set insert $insPos
    $w mark unset $insPos
    $w mark unset $endPos
}

# ::tk::TextInsertSelection --
# This procedure inserts the selection.
#
# Arguments:
# w -		The text window.
# x, y - 	Position of the mouse.
# selection	atom name of the selection

proc ::tk::TextInsertSelection {w selection} {
    if {[catch {GetSelection $w $selection} sel]} {
	return
    }
    set oldSeparator [$w cget -autoseparators]
    if {$oldSeparator} {
	$w configure -autoseparators 0
	$w edit separator
    }
    if {$selection eq "CLIPBOARD" && [tk windowingsystem] ne "x11"} {
	catch { TextDelete $w sel.first sel.last }
    }
    $w insert insert $sel
    if {$oldSeparator} {
	$w edit separator
	$w configure -autoseparators 1
    }
}

# ::tk_textInsert --
# This procedure supports the insertion of text with hyphen information.
#
# Arguments:
# w -		The text window.
# args -	Arguments for text insertion.

proc ::tk_textInsert {w args} {
    # Use an internal command:
    uplevel [list $w tk_textInsert {*}$args]
}

# ::tk_textReplace --
# This procedure supports the replacement of text with hyphen information.
#
# Arguments:
# w -		The text window.
# args -	Arguments for text insertion.

proc ::tk_textReplace {w args} {
    # Use an internal command:
    uplevel [list $w tk_textReplace {*}$args]
}

# ::tk_textRebindMouseWheel --
# This procedure is rebinding the mouse wheel events of the embedded
# window to the container (the text widget). This is a quite important
# convenience function, because the user might not be interested in
# the internal handlings.
#
# Arguments:
# text -	The container (text widget), send mouse wheel events
#		to this container.
# args -	Zero or more embedded windows. If none is specified,
#		then rebind all the windows that are currently embedded.

proc tk_textRebindMouseWheel {w args} {
    if {[llength $args] == 0} {
	set args [$w window names]
    }
    foreach ew $args {
	set cls [winfo class $w]
	foreach ev {MouseWheel Option-MouseWheel Shift-MouseWheel Shift-Option-MouseWheel} {
	    if {[string length [bind $w <$ev>]] || [string length [bind $cls <$ev>]]} {
		bind $ew <$ev> [list event generate $w <$ev> -delta %D]
	    }
	}
	foreach ev {4 5 Shift-4 Shift-5} {
	    if {[string length [bind $w <$ev>]] || [string length [bind $cls <$ev>]]} {
		bind $ew <$ev> [list event generate $w <$ev>]
	    }
	}
    }
}

# ::tk_mergeRange --
# This procedure is merging a range into a sorted list of ranges.
# If given range is adjacent to, or intersecting a range in given
# list, then it will be amalgamated.
#
# Arguments:
# rangeListVar -	Name of variable containing the list of ranges.
# newRange -		New range which should be merged into given list.

proc tk_mergeRange {rangeListVar newRange} {
    upvar $rangeListVar ranges

    if {![info exists ranges]} {
	lappend ranges $newRange
	return $ranges
    }

    lassign $newRange s e
    lassign [split $s .] sline scol
    lassign [split $e .] eline ecol
    set newRangeList {}
    set n [llength $ranges]

    for {set i 0} {$i < $n} {incr i} {
	set range [lindex $ranges $i]
	lassign $range s1 e1
	lassign [split $s1 .] sline1 scol1
	lassign [split $e1 .] eline1 ecol1

	# [$w compare "$e+1i" < $s1]
	if {$eline < $sline1 || ($eline == $sline1 && $ecol + 1 < $scol1)} {
	    lappend newRangeList [list $s $e]
	    lappend newRangeList {*}[lrange $ranges $i end]
	    set ranges $newRangeList
	    return $newRangeList
	}
	# [$w compare $s <= "$e1+1i"]
	if {$sline < $eline1 || ($sline == $eline1 && $scol <= $ecol1 + 1)} {
	    # [$w compare $s > $s1]
	    if {$sline > $sline1 || ($sline == $sline1 && $scol > $scol1)} {
		set s $s1; set sline $sline1; set scol $scol1
	    }
	    # [$w compare $e < $e1]
	    if {$eline < $eline1 || ($eline == $eline1 && $ecol < $ecol1)} {
		set e $e1; set eline $eline1; set ecol $ecol1
	    }
	} else {
	    lappend newRangeList $range
	}
    }

    lappend newRangeList [list $s $e]
    set ranges $newRangeList
    return $newRangeList
}

# vi:set ts=8 sw=4:
