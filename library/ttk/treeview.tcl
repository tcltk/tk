#
# ttk::treeview widget bindings and utilities.
#

namespace eval ttk::treeview {
    variable State

    # Enter/Leave/Motion
    #
    set State(activeWidget)	{}
    set State(activeHeading)	{}

    # Press/drag/release:
    #
    set State(pressMode)	none
    set State(pressX)		0

    # For pressMode == "resize"
    set State(resizeColumn)	#0

    # For pressmode == "heading"
    set State(heading)	{}

    set State(cellAnchorOp)	"set"
    set State(cellCurrent)	{}
    set State(current)		{}

    # Copy the layouts from Treeview to CheckTreeview
    foreach _from {Treeview Item Cell Heading Row Separator}  {
	set _to [expr {$_from ne "Treeview" ? "CheckTreeview.$_from" : "CheckTreeview"}]
	ttk::style layout $_to [ttk::style layout $_from]
	ttk::style configure $_to {*}[ttk::style configure $_from]
	ttk::style map $_to {*}[ttk::style map $_from]
    }
    unset _from _to

    # Create CheckTreeview Item
    ttk::style layout CheckTreeview.Item {
	Treeitem.padding -sticky nswe -children {
	    Treeitem.indicator -side left -sticky {}
	    Checkbutton.indicator -side left -sticky {}
	    Treeitem.image -side left -sticky {}
	    Treeitem.text -side left -sticky {}
	}
    }
}


#
## Widget bindings.
#

# Mouse button bindings
bind Treeview	<Motion>		{ ::ttk::treeview::Motion %W %x %y }
bind Treeview	<B1-Leave>		{}
bind Treeview	<Leave>			{ ::ttk::treeview::ActivateHeading {} {}}
bind Treeview	<Button-1>		{ ::ttk::treeview::Press %W %x %y }
bind Treeview	<Double-Button-1>	{ ::ttk::treeview::DoubleClick %W %x %y }
bind Treeview	<ButtonRelease-1>	{ ::ttk::treeview::Release %W %x %y }
bind Treeview	<B1-Motion>		{ ::ttk::treeview::Drag %W %x %y }
bind Treeview	<Shift-Button-1>	{ ::ttk::treeview::Select %W %x %y extend }
bind Treeview	<<ToggleSelection>>	{ ::ttk::treeview::Select %W %x %y toggle }

# Left/Right arrow key bindings (none, shift, control, control+shift)
bind Treeview	<<PrevChar>>		{ ::ttk::treeview::KeyNav %W left }
bind Treeview	<<NextChar>>		{ ::ttk::treeview::KeyNav %W right }
bind Treeview	<<SelectPrevChar>>	{ ::ttk::treeview::SelectionExtend %W left }
bind Treeview	<<SelectNextChar>>	{ ::ttk::treeview::SelectionExtend %W right }
bind Treeview	<<PrevWord>>		{ ::ttk::treeview::KeyNav %W pageLeft }
bind Treeview	<<NextWord>>		{ ::ttk::treeview::KeyNav %W pageRight }
bind Treeview	<<SelectPrevWord>>	{ ::ttk::treeview::SelectionExtend %W pageLeft }
bind Treeview	<<SelectNextWord>>	{ ::ttk::treeview::SelectionExtend %W pageRight }

# Up/down arrow key bindings (none, shift, control, control+shift)
bind Treeview	<<PrevLine>>		{ ::ttk::treeview::KeyNav %W up }
bind Treeview	<<NextLine>>		{ ::ttk::treeview::KeyNav %W down }
bind Treeview	<<SelectPrevLine>>	{ ::ttk::treeview::SelectionExtend %W up }
bind Treeview	<<SelectNextLine>>	{ ::ttk::treeview::SelectionExtend %W down }
bind Treeview	<<PrevPara>>		{ ::ttk::treeview::KeyNav %W pageTop }
bind Treeview	<<NextPara>>		{ ::ttk::treeview::KeyNav %W pageBottom }
bind Treeview	<<SelectPrevPara>>	{ ::ttk::treeview::SelectionExtend %W pageTop }
bind Treeview	<<SelectNextPara>>	{ ::ttk::treeview::SelectionExtend %W pageBottom }

# Home/End key bindings (none, shift, control, control+shift)
bind Treeview	<<LineStart>>		{ ::ttk::treeview::KeyNav %W first }
bind Treeview	<<LineEnd>>		{ ::ttk::treeview::KeyNav %W last }
bind Treeview	<<SelectLineStart>>	{ ::ttk::treeview::SelectionExtend %W first }
bind Treeview	<<SelectLineEnd>>	{ ::ttk::treeview::SelectionExtend %W last }
bind Treeview	<Control-Home>		{ ::ttk::treeview::KeyNav %W top }
bind Treeview	<Control-End>		{ ::ttk::treeview::KeyNav %W bottom }
bind Treeview	<Control-Shift-Home>	{ ::ttk::treeview::SelectionExtend %W top }
bind Treeview	<Control-Shift-End>	{ ::ttk::treeview::SelectionExtend %W bottom }

# Page Up/Down key bindings (none, shift, control, control+shift, alternate)
bind Treeview	<Prior>			{ ::ttk::treeview::PageNav %W up }
bind Treeview	<Next>			{ ::ttk::treeview::PageNav %W down }
bind Treeview	<Shift-Prior>		{ ::ttk::treeview::PageNav %W left }
bind Treeview	<Shift-Next>		{ ::ttk::treeview::PageNav %W right }
bind Treeview	<Control-Prior>		{ ::ttk::treeview::PageNav %W pageTop }
bind Treeview	<Control-Next>		{ ::ttk::treeview::PageNav %W pageBottom }
bind Treeview	<Control-Shift-Prior>	{ ::ttk::treeview::PageNav %W pageLeft }
bind Treeview	<Control-Shift-Next>	{ ::ttk::treeview::PageNav %W pageRight }
bind Treeview	<Alt-Prior>		{ ::ttk::treeview::PageNav %W left; break }
bind Treeview	<Alt-Next>		{ ::ttk::treeview::PageNav %W right; break }

# Scroll Lock bindings
bind Treeview	<Mod3-Up>		{ %W yview scroll -1 units }
bind Treeview	<Mod3-Down>		{ %W yview scroll 1 units }
bind Treeview	<Mod3-Left>		{ %W xview scroll -10 units }
bind Treeview	<Mod3-Right>		{ %W xview scroll 10 units }
bind Treeview	<Mod3-Prior>		{ %W yview scroll -1 pages }
bind Treeview	<Mod3-Next>		{ %W yview scroll 1 pages }
bind Treeview	<Mod3-Control-Prior>	{ %W yview moveto 0.0 }
bind Treeview	<Mod3-Control-Next>	{ %W yview moveto 1.0 }
bind Treeview	<Mod3-Home>		{ %W xview scroll -1 pages }
bind Treeview	<Mod3-End>		{ %W xview scroll 1 pages }
bind Treeview	<Mod3-Control-Home>	{ %W xview moveto 0.0 }
bind Treeview	<Mod3-Control-End>	{ %W xview moveto 1.0 }

# Other keys
bind Treeview	<Return>		{ ::ttk::treeview::ToggleFocus %W toggle }
bind Treeview	<space>			{ ::ttk::treeview::ToggleFocus %W select }
bind Treeview	<<Invoke>>		{ ::ttk::treeview::ToggleFocus %W select }
bind Treeview	<Tab>			{ ::ttk::treeview::KeyNav %W right; break }
bind Treeview	<Shift-Tab>		{ ::ttk::treeview::KeyNav %W left; break }
#bind Treeview	<Return>		{ ::ttk::treeview::KeyNav %W down; break }
#bind Treeview	<Shift-Return>		{ ::ttk::treeview::KeyNav %W up; break }
bind Treeview	<Control-Return>	{ ::ttk::treeview::ToggleFocus %W toggle }
bind Treeview	<Control-Tab>		[bind all <<NextWindow>>]
bind Treeview	<Control-Shift-Tab>	[bind all <<PrevWindow>>]
bind Treeview	<minus>			{ ::ttk::treeview::CloseItem %W {} }
bind Treeview	<plus>			{ ::ttk::treeview::OpenItem %W {} }
bind Treeview	<asterisk>		{ ::ttk::treeview::OpenItem %W {} -recurse }

# Other selection functions
bind Treeview	<<SelectAll>>		{ ::ttk::treeview::SelectionSet %W all }
bind Treeview	<<SelectNone>>		{ ::ttk::treeview::SelectionSet %W none }
bind Treeview	<Shift-space>		{ ::ttk::treeview::SelectionSet %W row }
bind Treeview	<Control-space>		{ ::ttk::treeview::SelectionSet %W column }

# Mousewheel and TouchpadScroll
ttk::copyBindings TtkScrollable Treeview

#
## Binding procedures.
#

# Get first column number
proc ::ttk::treeview::FirstColumnNum {w} {
    return [expr {"tree" ni [$w cget -show]}]
}
proc ::ttk::treeview::FirstColumnId {w} {
    return [format "#%d" [FirstColumnNum $w]]
}

# Get last column number
proc ::ttk::treeview::LastColumnNum {w} {
    set columns [$w cget -displaycolumns]
    if {$columns eq "#all"} {
	set columns [$w cget -columns]
    }
    return [llength $columns]
}
proc ::ttk::treeview::LastColumnId {w} {
    return [format "#%d" [LastColumnNum $w]]
}

# Get current column number
proc ::ttk::treeview::GetColumn {w} {
    variable State
    lassign [$w cellselection anchor] anchor colAnchor
    lassign $State(cellCurrent) current colCurrent

    # Just in case, give it a valid value
    if {$colAnchor eq ""} {
	set colAnchor "#1"
    }
    if {$colCurrent eq ""} {
	set colCurrent $colAnchor
    }
    scan $colCurrent "#%d" colNum
    return $colNum
}

# Get first visible row
proc ::ttk::treeview::PageTop {w} {
    set item ""
    set rh [ttk::style configure Treeview -rowheight]
    set y [expr {"headings" in [$w cget -show] ? $rh : 1}]
    for {} {$y < [winfo height $w]} {incr y $rh} {
	set item [$w identify item 10 $y]
	if {$item ne ""} break
    }
    return $item
}

# Get last visible row
proc ::ttk::treeview::PageBottom {w} {
    set item ""
    set rh [expr {[ttk::style configure Treeview -rowheight] * -1}]
    set y [expr {[winfo height $w] + $rh}]
    for {} {$y > 0} {incr y $rh} {
	set item [$w identify item 10 $y]
	if {$item ne ""} break
    }
    return $item
}

# Get top row item
proc ::ttk::treeview::TopItem {w} {
    return [$w id {} 0]
}

# Get bottom row item
proc ::ttk::treeview::BottomItem {w} {
    set item [$w id {} last]
    while {[$w item $item -open] && [$w haschildren $item]} {
	set item [$w id $item last]
    }
    return $item
}

# Get widget width
proc ::ttk::treeview::GetWidth {w} {
    # Get widget width
    set width 0
    set list [list]
    for {set i [FirstColumnNum $w]} {$i <= [LastColumnNum $w]} {incr i} {
	lappend list $i $width [incr width [$w column [format "#%d" $i] -width]]
    }
    return [list $list $width]
}

# Get left most visible column
proc ::ttk::treeview::PageLeft {w} {
    lassign [$w xview] start end
    lassign [GetWidth $w] list width
    set left [expr {int($start * $width)}]

    foreach {c x1 x2} $list {
	if {$x1 >= $left} {
	    break
	}
    }
    return $c
}

# Get right most visible column
proc ::ttk::treeview::PageRight {w} {
    lassign [$w xview] start end
    lassign [GetWidth $w] list width
    set right [expr {int($end * $width)}]
    set new 0

    foreach {c x1 x2} $list {
	if {$x2 > $right} {
	    break
	}
	set new $c
    }
    return $new
}

#
# KeyNav -- Keyboard navigation to move focus and selected item/cell
#
proc ::ttk::treeview::KeyNav {w dir} {
    set focus [$w focus]
    if {$focus eq ""} { return }

    set cellmode [expr {[$w cget -selecttype] eq "cell"}]
    if {$cellmode} {
	set colNum [GetColumn $w]
    }

    switch -- $dir {
	left {
	    # Move left one cell or close item if open
	    if {$cellmode} {
		if {$colNum > [FirstColumnNum $w]} {
		    incr colNum -1
		}
	    } elseif {[$w item $focus -open] && [$w haschildren $focus]} {
		CloseItem $w $focus
	    } else {
		set focus [$w parent $focus]
	    }
	}
	right {
	    # Move right one cell or open item if closed
	    if {$cellmode} {
		# Move to next cell
		if {$colNum < [LastColumnNum $w]} {
		    incr colNum
		}
	    } elseif {![$w item $focus -open] && [$w haschildren $focus]} {
		OpenItem $w $focus
	    }
	}
	up {
	    # Move up one item/cell
	    set focus [$w before $focus]
	}
	down {
	    # Move down one item/cell
	    set focus [$w after $focus]
	}
	first {
	    # Move to first cell in item or first child in parent
	    if {$cellmode} {
		set colNum [FirstColumnNum $w]
	    } else {
		set focus [$w id [$w parent $focus] first]
	    }
	}
	last {
	    # Move to last cell in item or last child in parent
	    if {$cellmode} {
		set colNum [LastColumnNum $w]
	    } else {
		set focus [$w id [$w parent $focus] last]
	    }
	}
	top {
	    # Move to topmost item or cell in col
	    $w yview moveto 0
	    set focus [TopItem $w]
	}
	bottom {
	    # Move to bottom-most item or cell in col
	    $w yview moveto 1
	    set focus [BottomItem $w]
	}
	pageLeft {
	    # Move to first column in current screen view
	    if {$cellmode} {
		set colNum [PageLeft $w]
	    }
	}
	pageRight {
	    # Move to last column in current screen view
	    if {$cellmode} {
		set colNum [PageRight $w]
	    }
	}
	pageTop {
	    # Move to first row in current screen view
	    set focus [PageTop $w]
	}
	pageBottom {
	    # Move to last row in current screen view
	    set focus [PageBottom $w]
	}
    }

    # Do select
    if {$focus ne {}} {
	if {$cellmode} {
	    SelectOp $w $focus [list $focus [format "#%d" $colNum]] moveto
	} else {
	    SelectOp $w $focus "" moveto
	}
    }
}

#
# PageNav -- Scroll view and move focus/selected item
#
proc ::ttk::treeview::PageNav {w dir} {
    set focus [$w focus]
    if {$focus eq ""} { return }

    set cellmode [expr {[$w cget -selecttype] eq "cell"}]
    if {$cellmode} {
	set colId [format "#%d" [GetColumn $w]]
    }

    if {$dir in [list down left right up]} {
	if {$cellmode} {
	    lassign [$w bbox $focus $colId] x y width height
	} else {
	    lassign [$w bbox $focus] x y width height
	}
	if {$x ne ""} {
	    incr x [expr {$width / 2}]
	    incr y [expr {$height / 2}]
	} else {
	    switch -- $dir {
		"up" {set dir pageTop}
		"down" {set dir pageBottom}
	    }
	}
    }

    switch -- $dir {
	first {
	    # Move to first cell in item or first child in parent
	    if {$cellmode} {
		set colNum [FirstColumnNum $w]
	    } else {
		set focus [$w id [$w parent $focus] first]
	    }
	}
	last {
	    # Move to last cell in item or last child in parent
	    if {$cellmode} {
		set colNum [LastColumnNum $w]
	    } else {
		set focus [$w id [$w parent $focus] last]
	    }
	}
	left {
	    # Move left 1 page & select cell
	    lassign [$w xview] start end
	    if {$start > 0.0} {
		$w xview scroll -1 pages
		update idletasks
		if {$cellmode && $x ne ""} {
		    # Select cell at same coord
		    lassign [$w identify cell $x $y] focus colId
		}
	    } else {
		if {$cellmode} {
		    # Select first cell if at left edge already
		    set colId [FirstColumnId $w]
		}
	    }
	}
	right {
	    # Move right 1 page & select cell
	    lassign [$w xview] start end
	    if {$end < 1.0} {
		$w xview scroll 1 pages
		update idletasks
		if {$cellmode && $x ne ""} {
		    # Select cell at same coord
		    lassign [$w identify cell $x $y] focus colId
		}
	    } else {
		if {$cellmode} {
		    # Select last cell if at right edge already
		    set colId [LastColumnId $w]
		}
	    }
	}
	up {
	    # Move up 1 page & select cell
	    lassign [$w yview] start end
	    if {$start > 0.0} {
		# Scroll up 1 page and select item/cell at same coord
		$w yview scroll -1 pages
		update idletasks
		set focus [$w identify item $x $y]
	    } else {
		# Select topmost item/cell if at top already
		set focus [$w id {} first]
	    }
	}
	down {
	    # Move down 1 page & select cell
	    lassign [$w yview] start end
	    if {$end < 1.0} {
		# Scroll down 1 page and select item/cell at same coord
		$w yview scroll 1 pages
		update idletasks
		set focus [$w identify item $x $y]
	    } else {
		# Select bottom-most item/cell if at bottom already
		set focus [$w id {} last]
		while {[$w haschildren $focus] && [$w item $focus -open]} {
		    set focus [$w id $focus last]
		}
	    }
	}
	first {
	    # Move to first cell in item or first child in parent
	    if {$cellmode} {
		set colNum [FirstColumnNum $w]
	    } else {
		set focus [$w id [$w parent $focus] first]
	    }
	}
	last {
	    # Move to last cell in item or last child in parent
	    if {$cellmode} {
		set colNum [LastColumnNum $w]
	    } else {
		set focus [$w id [$w parent $focus] last]
	    }
	}
	top {
	    # Move to & select topmost item/cell
	    $w yview moveto 0
	    set focus [TopItem $w]
	}
	bottom {
	    # Move to & select bottom-most item/cell
	    $w yview moveto 1
	    set focus [BottomItem $w]
	}
	pageLeft {
	    # Move to & select leftmost item/cell in current screen view
	    if {$cellmode} {
		set colNum [PageLeft $w]
	    }
	}
	pageRight {
	    # Move to & select rightmost item/cell in current screen view
	    if {$cellmode} {
		set colNum [PageRight $w]
	    }
	}
	pageTop {
	    # Move to & select topmost item/cell in current screen view
	    set focus [PageTop $w]
	}
	pageBottom {
	    # Move to & select bottommost item/cell in current screen view
	    set focus [PageBottom $w]
	}
    }

    # Do select
    if {$focus ne ""} {
	if {$cellmode} {
	    SelectOp $w $focus [list $focus $colId] moveto
	} else {
	    SelectOp $w $focus "" moveto
	}
    }
}

#
# SelectionExtend -- Extend item/cell selection
#
proc ::ttk::treeview::SelectionExtend {w dir} {
    variable State
    set focus [$w focus]

    set cellmode [expr {[$w cget -selecttype] eq "cell"}]
    if {$cellmode} {
	set colNum [GetColumn $w]
    }

    if {$cellmode} {
	lassign $State(cellCurrent) current colCurrent
	if {$focus ne $current && $current ne ""} {
	    set focus $current
	    scan $colCurrent "#%d" colNum
	}
    } else {
	set current $State(current)
	if {$focus ne $current && $current ne ""} {
	    set focus $current
	}
    }

    switch -- $dir {
	first {
	    # Move to first cell in item or first child in parent
	    if {$cellmode} {
		set colNum [FirstColumnNum $w]
	    } else {
		set focus [$w id [$w parent $focus] first]
	    }
	}
	last {
	    # Move to last cell in item or last child in parent
	    if {$cellmode} {
		set colNum [LastColumnNum $w]
	    } else {
		set focus [$w id [$w parent $focus] last]
	    }
	}
	left {
	    # Extend selection to prev cell
	    if {$cellmode} {
		if {$colNum > [FirstColumnNum $w]} {
		    incr colNum -1
		}
	    }
	}
	right {
	    # Extend selection to next cell
	    if {$cellmode} {
		if {$colNum < [LastColumnNum $w]} {
		    incr colNum
		}
	    }
	}
	up {
	    # Extend selection to prev item
	    set focus [$w before $focus]
	}
	down {
	    # Extend selection to next item
	    set focus [$w after $focus]
	}
	first {
	    # Move to first cell in item or first child in parent
	    if {$cellmode} {
		set colNum [FirstColumnNum $w]
	    } else {
		set focus [$w id [$w parent $focus] first]
	    }
	}
	last {
	    # Move to last cell in item or last child in parent
	    if {$cellmode} {
		set colNum [LastColumnNum $w]
	    } else {
		set focus [$w id [$w parent $focus] last]
	    }
	}
	top {
	    # Select all cells/items from focus to top
	    set focus [TopItem $w]
	}
	bottom {
	    # Select all cells/items from focus to bottom
	    set focus [BottomItem $w]
	}
	pageLeft {
	    # Select all cells from current to first column in current screen view
	    if {$cellmode} {
		set colNum [PageLeft $w]
	    }
	}
	pageRight {
	    # Select all cells from current to last column in current screen view
	    if {$cellmode} {
		set colNum [PageRight $w]
	    }
	}
	pageTop {
	    # Select all cells from current to first row in current screen view
	    set focus [PageTop $w]
	}
	pageBottom {
	    # Select all cells from current to last row in current screen view
	    set focus [PageBottom $w]
	}
    }

    # Do select
    if {$focus ne ""} {
	if {$cellmode} {
	    SelectOp $w $focus [list $focus [format "#%d" $colNum]] extend
	} else {
	    SelectOp $w $focus "" extend
	}
    }
}

#
# SelectionSet -- Set special selection types
#
proc ::ttk::treeview::SelectionSet {w fn} {
    variable State
    set focus [$w focus]
    set mode [$w cget -selectmode]

    if {($mode eq "single" && $fn ne "none")} {
	return
    } elseif {$mode ni [list "extended" "multiple"]} {
	return
    }

    set cellmode [expr {[$w cget -selecttype] eq "cell"}]

    switch -- $fn {
	all {
	    if {$cellmode} {
		$w cellselection set [list [TopItem $w] [FirstColumnId $w]] \
		    [list [BottomItem $w] [LastColumnId $w]]
	    } else {
		SelectAllItems $w {}
	    }
	}
	column {
	    if {$cellmode} {
		lassign $State(cellCurrent) current colCurrent
		$w cellselection set [list [TopItem $w] $colCurrent] \
		    [list [BottomItem $w] $colCurrent]
	    }
	}
	none {
	    if {$cellmode} {
		$w cellselection set {}
	    } else {
		$w selection set {}
	    }
	}
	row {
	    if {$focus ne "" && $cellmode} {
		$w cellselection set [list $focus [FirstColumnId $w]] \
		    [list $focus [LastColumnId $w]]
	    }
	}
    }
}

proc ::ttk::treeview::SelectAllItems {w item} {
    if {$item eq "" || ([$w item $item -open] && [$w haschildren $item])} {
	set list [$w children $item]
	$w selection add $list
	foreach child $list {
	    SelectAllItems $w $child
	}
    }
}

#
# Motion -- pointer motion binding.
#	Sets cursor, active element ...
#
proc ::ttk::treeview::Motion {w x y} {
    variable State

    ttk::saveCursor $w State(userConfCursor) [ttk::cursor hresize]

    set cursor $State(userConfCursor)
    set activeHeading {}

    switch -- [$w identify region $x $y] {
	separator { set cursor hresize }
	heading { set activeHeading [$w identify column $x $y] }
    }

    ttk::setCursor $w $cursor
    ActivateHeading $w $activeHeading
}

## ActivateHeading -- track active heading element
#
proc ::ttk::treeview::ActivateHeading {w heading} {
    variable State

    if {$w != $State(activeWidget) || $heading != $State(activeHeading)} {
	if {[winfo exists $State(activeWidget)] && $State(activeHeading) != {}} {
	    # It may happen that $State(activeHeading) no longer corresponds
	    # to an existing display column. This happens for instance when
	    # changing -displaycolumns in a bound script when this change
	    # triggers a <Leave> event. A proc checking if the display column
	    # $State(activeHeading) is really still present or not could be
	    # written but it would need to check several special cases:
	    #   a. -displaycolumns "#all" or being an explicit columns list
	    #   b. column #0 display is not governed by the -displaycolumn
	    #      list but by the value of the -show option
	    # --> Let's rather catch the following line.
	    catch {$State(activeWidget) heading $State(activeHeading) state !active}
	}
	if {$heading != {}} {
	    $w heading $heading state active
	}
	set State(activeHeading) $heading
	set State(activeWidget) $w
    }
}

#
# IndentifyCell -- Locate the cell at coordinate
#	Only active when -selecttype is "cell", and leaves cell empty otherwise.
#       Down the call chain it is enough to check cell to know the selecttype.
proc ::ttk::treeview::IdentifyCell {w x y} {
    set cell {}
    if {[$w cget -selecttype] eq "cell"} {
	# Later handling assumes that the column in the cell ID is of the
	# format #N, which is always the case from "identify cell"
	set cell [$w identify cell $x $y]
    }
    return $cell
}

#
# Select $w $x $y $selectop
#	Binding procedure for selection operations.
#	See "Selection modes", below.
#
proc ::ttk::treeview::Select {w x y op} {
    if {[$w cget -selectmode] ne "extended"} {
	return
    }

    if {[set item [$w identify item $x $y]] ne "" } {
	set cell [IdentifyCell $w $x $y]
	SelectOp $w $item $cell $op
    }
}

#
# DoubleClick -- Double-Button-1 binding.
#
proc ::ttk::treeview::DoubleClick {w x y} {
    if {[set row [$w identify item $x $y]] ne ""} {
	Toggle $w $row
    } else {
	Press $w $x $y ;# perform single-click action
    }
}

#
# Press -- Button binding.
#
proc ::ttk::treeview::Press {w x y} {
    focus $w
    switch -- [$w identify region $x $y] {
	nothing { }
	heading { heading.press $w $x $y }
	separator { resize.press $w $x $y }
	tree -
	cell {
	    set item [$w identify item $x $y]
	    set cell [IdentifyCell $w $x $y]

	    SelectOp $w $item $cell choose
	    switch -glob -- [$w identify element $x $y] {
		*indicator -
		*disclosure { Toggle $w $item }
	    }
	}
    }
}

#
# Drag -- B1-Motion binding
#
proc ::ttk::treeview::Drag {w x y} {
    variable State
    switch $State(pressMode) {
	resize	{ resize.drag $w $x }
	heading	{ heading.drag $w $x $y }
    }
}

proc ::ttk::treeview::Release {w x y} {
    variable State
    switch $State(pressMode) {
	resize	{ resize.release $w $x }
	heading	{ heading.release $w }
    }
    set State(pressMode) none
    Motion $w $x $y
}

#
## Interactive column resizing.
#
proc ::ttk::treeview::resize.press {w x y} {
    variable State
    set State(pressMode) "resize"
    set State(resizeColumn) [$w identify column $x $y]
}

proc ::ttk::treeview::resize.drag {w x} {
    variable State
    $w drag $State(resizeColumn) $x
}

proc ::ttk::treeview::resize.release {w x} {
    $w drop
}

#
## Heading activation.
#

proc ::ttk::treeview::heading.press {w x y} {
    variable State
    set column [$w identify column $x $y]
    set State(pressMode) "heading"
    set State(heading) $column
    $w heading $column state pressed
}

proc ::ttk::treeview::heading.drag {w x y} {
    variable State
    if {   [$w identify region $x $y] eq "heading"
	&& [$w identify column $x $y] eq $State(heading)
    } {
	$w heading $State(heading) state pressed
    } else {
	$w heading $State(heading) state !pressed
    }
}

proc ::ttk::treeview::heading.release {w} {
    variable State
    if {[lsearch -exact [$w heading $State(heading) state] pressed] >= 0} {
	after 0 [$w heading $State(heading) -command]
    }
    $w heading $State(heading) state !pressed
}

#
## Selection modes.
#

#
# SelectOp $w $item $cell [ moveto | choose | extend | toggle ] --
#	Dispatch to appropriate selection operation
#	depending on current value of -selectmode.
#
# Where:moveto = Keyboard traverse move to cell and select it
#	choose = Select item or cell
#	toggle = Button or keyboard open/close item and select it
#	extend = Extend selection to include item or cell
#
proc ::ttk::treeview::SelectOp {w item cell op} {
    select.$op.[$w cget -selectmode] $w $item $cell
}

#
# -selectmode none:
#
proc ::ttk::treeview::select.moveto.none {w item cell} { BrowseTo $w $item $cell none }
proc ::ttk::treeview::select.choose.none {w item cell} { BrowseTo $w $item $cell none }
proc ::ttk::treeview::select.toggle.none {w item cell} { BrowseTo $w $item $cell none }
proc ::ttk::treeview::select.extend.none {w item cell} { BrowseTo $w $item $cell none }

#
# -selectmode browse:
#
proc ::ttk::treeview::select.moveto.browse {w item cell} { BrowseTo $w $item $cell }
proc ::ttk::treeview::select.choose.browse {w item cell} { BrowseTo $w $item $cell }
proc ::ttk::treeview::select.toggle.browse {w item cell} { BrowseTo $w $item $cell }
proc ::ttk::treeview::select.extend.browse {w item cell} { BrowseTo $w $item $cell }

#
# -selectmode single:
#
proc ::ttk::treeview::select.moveto.single {w item cell} { BrowseTo $w $item $cell }
proc ::ttk::treeview::select.choose.single {w item cell} { BrowseTo $w $item $cell }
proc ::ttk::treeview::select.toggle.single {w item cell} { BrowseTo $w $item $cell toggle }
proc ::ttk::treeview::select.extend.single {w item cell} { BrowseTo $w $item $cell }

#
# -selectmode multiple:
#
proc ::ttk::treeview::select.moveto.multiple {w item cell} { BrowseTo $w $item $cell none }
proc ::ttk::treeview::select.choose.multiple {w item cell} { BrowseTo $w $item $cell toggle }
proc ::ttk::treeview::select.toggle.multiple {w item cell} { BrowseTo $w $item $cell toggle }
proc ::ttk::treeview::select.extend.multiple {w item cell} { ExtendTo $w $item $cell add }

#
# -selectmode extended:
#
proc ::ttk::treeview::select.moveto.extended {w item cell} { BrowseTo $w $item $cell }
proc ::ttk::treeview::select.choose.extended {w item cell} { BrowseTo $w $item $cell }
proc ::ttk::treeview::select.toggle.extended {w item cell} { BrowseTo $w $item $cell toggle }
proc ::ttk::treeview::select.extend.extended {w item cell} { ExtendTo $w $item $cell }

#
## User interaction utilities.
#

#
# OpenItem, CloseItem -- Set the open state of an item, generate event
#

proc ::ttk::treeview::OpenItem {w item args} {
    if {$item ne ""} {
	$w focus $item
    } else {
	set item [$w focus]
    }
    event generate $w <<TreeviewOpen>>
    $w expand {*}$args [list $item]
}

proc ::ttk::treeview::CloseItem {w item} {
    if {$item ne ""} {
	$w focus $item
    } else {
	set item [$w focus]
    }
    $w collapse [list $item]
    event generate $w <<TreeviewClose>>
}

#
# Toggle -- toggle opened/closed state of item
#
proc ::ttk::treeview::Toggle {w item} {
    # don't allow toggling on indicators that
    # are not present in front of leaf items
    if {![$w haschildren $item]} {
	return
    }
    # not a leaf, toggle!
    if {[$w item $item -open]} {
	CloseItem $w $item
    } else {
	OpenItem $w $item
    }
}

#
# ToggleFocus -- toggle opened/closed state of focus item
#
proc ::ttk::treeview::ToggleFocus {w op} {
    set focus [$w focus]
    if {$focus eq "" || [$w cget -selectmode] in [list "none" "browse"]} {
	return
    }

    set cellmode [expr {[$w cget -selecttype] eq "cell"}]
    if {$cellmode} {
	set colId [list $focus [format "#%d" [GetColumn $w]]]
    } else {
	set colId ""
    }

    if {$op eq "toggle" && [$w haschildren $focus]} {
	Toggle $w $focus
    } else {
	SelectOp $w $focus $colId toggle
    }
}

#
# BrowseTo -- navigate to specified item; set focus and selection
#
proc ::ttk::treeview::BrowseTo {w item cell {op set}} {
    variable State

    if {$item ne [$w focus]} {
	$w see $item
	$w focus $item
    }
    if {$op ne "none"} {
	array set State [list cellAnchorOp $op cellCurrent $cell current $item]
	if {$cell ne ""} {
	    $w cellselection anchor $cell
	    $w cellselection $op [list $cell]
	    # See column
	} else {
	    $w selection anchor $item
	    $w selection $op [list $item]
	}
    }
}

#
# ExtendTo -- Extend selection
#
proc ::ttk::treeview::ExtendTo {w item cell {op set}} {
    variable State

    if {$cell ne ""} {
	set State(cellCurrent) $cell
	set anchor [$w cellselection anchor]
	if {$anchor ne ""} {
	    $w cellselection $op $anchor $cell
	} else {
	    BrowseTo $w $item $cell $op
	}
    } else {
	set State(current) $item
	set anchor [$w selection anchor]
	if {$anchor ne ""} {
	    $w selection $op $anchor $item
	    $w see $item
	} else {
	    BrowseTo $w $item $cell $op
	}
    }
}

#*EOF*
