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
    set State(cursor)	{}

    set State(current)		{}
    set State(currentCell)	{}

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

# Clipboard and selection functions
bind Treeview	<<Copy>>		{ ::ttk::treeview::CopyToClipboard %W }

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

# Get first column number or id
proc ::ttk::treeview::FirstColumnNum {w} {
    return [expr {"tree" ni [$w cget -show]}]
}
proc ::ttk::treeview::FirstColumnId {w} {
    return [format "#%d" [FirstColumnNum $w]]
}

# Get last column number or id
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

# Get current item
proc ::ttk::treeview::GetCurrent {w skip} {
    variable State
    set anchor [$w selection anchor]
    set focus [$w focus]

    if {$State(current) ne "" && !$skip} {
	set current $State(current)
    } elseif {$focus ne ""} {
	set current $focus
    } else {
	set current $anchor
    }

    # Just in case, give it a valid value
    if {$current eq ""} {
	set current [$w identifier {} 0]
    }
    return $current
}

# Get current cell
proc ::ttk::treeview::GetCurrentCell {w skip} {
    variable State
    lassign [$w cellselection anchor] anchor colAnchor
    lassign [$w cellfocus] focus colFocus

    if {$State(currentCell) ne "" && !$skip} {
	lassign $State(currentCell) current column
    } elseif {$colFocus ne ""} {
	set current $focus
	set column $colFocus
    } else {
	set current $anchor
	set column $colAnchor
    }

    # Just in case, give it a valid value
    if {$column eq ""} {
	set column [FirstColumnId $w]
    }
    if {[string index $column 0] eq "#"} {
	scan $column "#%d" colNum
    } elseif {[string is integer $column]} {
	set colNum $column
    } else {
	set list [$w cget -displaycolumns]
	if {$list eq "#all"} {
	    set list [$w cget -columns]
	}
	set colNum [lsearch $list $column]
	incr colNum
    }
    return [list $current $column $colNum]
}

# Get top most visible item
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

# Get bottom most visible item
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

# Get top item in widget
proc ::ttk::treeview::TopItem {w} {
    return [$w id {} 0]
}

# Get bottom item in widget
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
proc ::ttk::treeview::KeyNav {w fn} {
    set cellmode [expr {[$w cget -selecttype] eq "cell"}]
    if {$cellmode} {
	lassign [GetCurrentCell $w 1] current colId colNum
    } else {
	set current [GetCurrent $w 1]
    }
    if {$current eq ""} { return }

    switch -- $fn {
	left {
	    # Move left one cell or close item if open
	    if {$cellmode} {
		if {$colNum > [FirstColumnNum $w]} {
		    incr colNum -1
		}
	    } elseif {[$w item $current -open] && [$w haschildren $current]} {
		CloseItem $w $current
	    } else {
		set current [$w parent $current]
	    }
	}
	right {
	    # Move right one cell or open item if closed
	    if {$cellmode} {
		if {$colNum < [LastColumnNum $w]} {
		    incr colNum
		}
	    } elseif {![$w item $current -open] && [$w haschildren $current]} {
		OpenItem $w $current
	    }
	}
	up {
	    # Move up one item/cell
	    set current [$w before $current]
	}
	down {
	    # Move down one item/cell
	    set current [$w after $current]
	}
	first {
	    # Move to first cell in item or first child in parent
	    if {$cellmode} {
		set colNum [FirstColumnNum $w]
	    } else {
		set current [$w id [$w parent $current] first]
	    }
	}
	last {
	    # Move to last cell in item or last child in parent
	    if {$cellmode} {
		set colNum [LastColumnNum $w]
	    } else {
		set current [$w id [$w parent $current] last]
	    }
	}
	top {
	    # Move to top item in col
	    $w yview moveto 0
	    set current [TopItem $w]
	}
	bottom {
	    # Move to bottom item in col
	    $w yview moveto 1
	    set current [BottomItem $w]
	}
	pageLeft {
	    # Move to left most visible column
	    if {$cellmode} {
		set colNum [PageLeft $w]
	    }
	}
	pageRight {
	    # Move to right most visible column
	    if {$cellmode} {
		set colNum [PageRight $w]
	    }
	}
	pageTop {
	    # Move to top most visible item
	    set current [PageTop $w]
	}
	pageBottom {
	    # Move to bottom most visible item
	    set current [PageBottom $w]
	}
    }

    # Do select
    if {$current ne {}} {
	if {$cellmode} {
	    SelectOp $w $current [list $current [format "#%d" $colNum]] moveto
	} else {
	    SelectOp $w $current "" moveto
	}
    }
}

#
# PageNav -- Scroll view and move focus/selected item
#
proc ::ttk::treeview::PageNav {w fn} {
    set cellmode [expr {[$w cget -selecttype] eq "cell"}]
    if {$cellmode} {
	lassign [GetCurrentCell $w 1] current colId colNum
    } else {
	set current [GetCurrent $w 1]
    }
    if {$current eq ""} { return }

    if {$fn in [list down left right up]} {
	if {$cellmode} {
	    lassign [$w bbox $current $colId] x y width height
	} else {
	    lassign [$w bbox $current] x y width height
	}
	if {$x ne ""} {
	    incr x [expr {$width / 2}]
	    incr y [expr {$height / 2}]
	} else {
	    switch -- $fn {
		"up" {set fn pageTop}
		"down" {set fn pageBottom}
	    }
	}
    }

    switch -- $fn {
	left {
	    # Move left 1 page & select cell
	    lassign [$w xview] start end
	    if {$start > 0.0} {
		$w xview scroll -1 pages
		update idletasks
		if {$cellmode && $x ne ""} {
		    # Select cell at same coord
		    lassign [$w identify cell $x $y] current colId
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
		    lassign [$w identify cell $x $y] current colId
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
		set current [$w identify item $x $y]
	    } else {
		# Select topmost item/cell if at top already
		set current [$w id {} first]
	    }
	}
	down {
	    # Move down 1 page & select cell
	    lassign [$w yview] start end
	    if {$end < 1.0} {
		# Scroll down 1 page and select item/cell at same coord
		$w yview scroll 1 pages
		update idletasks
		set current [$w identify item $x $y]
	    } else {
		# Select bottom-most item/cell if at bottom already
		set current [$w id {} last]
		while {[$w haschildren $current] && [$w item $current -open]} {
		    set current [$w id $current last]
		}
	    }
	}
	first {
	    # Move to first cell in item or first child in parent
	    if {$cellmode} {
		set colId [FirstColumnId $w]
	    } else {
		set current [$w id [$w parent $current] first]
	    }
	}
	last {
	    # Move to last cell in item or last child in parent
	    if {$cellmode} {
		set colId [LastColumnId $w]
	    } else {
		set current [$w id [$w parent $current] last]
	    }
	}
	top {
	    # Move to & select topmost item/cell
	    $w yview moveto 0
	    set current [TopItem $w]
	}
	bottom {
	    # Move to & select bottom-most item/cell
	    $w yview moveto 1
	    set current [BottomItem $w]
	}
	pageLeft {
	    # Move to & select leftmost item/cell in current screen view
	    if {$cellmode} {
		set colId [format "#%d" [PageLeft $w]]
	    }
	}
	pageRight {
	    # Move to & select rightmost item/cell in current screen view
	    if {$cellmode} {
		set colId [format "#%d" [PageRight $w]]
	    }
	}
	pageTop {
	    # Move to & select topmost item/cell in current screen view
	    set current [PageTop $w]
	}
	pageBottom {
	    # Move to & select bottommost item/cell in current screen view
	    set current [PageBottom $w]
	}
    }

    # Do select
    if {$current ne ""} {
	if {$cellmode} {
	    SelectOp $w $current [list $current $colId] moveto
	} else {
	    SelectOp $w $current "" moveto
	}
    }
}

#
# SelectionExtend -- Extend item/cell selection
#
proc ::ttk::treeview::SelectionExtend {w fn} {
    set cellmode [expr {[$w cget -selecttype] eq "cell"}]
    if {$cellmode} {
	lassign [GetCurrentCell $w 0] current colId colNum
    } else {
	set current [GetCurrent $w 0]
    }
    if {$current eq ""} { return }

    switch -- $fn {
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
	    set current [$w before $current]
	}
	down {
	    # Extend selection to next item
	    set current [$w after $current]
	}
	first {
	    # Move to first cell in item or first child in parent
	    if {$cellmode} {
		set colNum [FirstColumnNum $w]
	    } else {
		set current [$w id [$w parent $current] first]
	    }
	}
	last {
	    # Move to last cell in item or last child in parent
	    if {$cellmode} {
		set colNum [LastColumnNum $w]
	    } else {
		set current [$w id [$w parent $current] last]
	    }
	}
	top {
	    # Select all cells/items from current to top
	    set current [TopItem $w]
	}
	bottom {
	    # Select all cells/items from current to bottom
	    set current [BottomItem $w]
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
	    set current [PageTop $w]
	}
	pageBottom {
	    # Select all cells from current to last row in current screen view
	    set current [PageBottom $w]
	}
    }

    # Do select
    if {$current ne ""} {
	if {$cellmode} {
	    SelectOp $w $current [list $current [format "#%d" $colNum]] extend
	} else {
	    SelectOp $w $current "" extend
	}
    }
}

#
# SelectionSet -- Set special selection types
#
proc ::ttk::treeview::SelectionSet {w fn} {
    set mode [$w cget -selectmode]
    if {($mode eq "single" && $fn ne "none")} {
	return
    } elseif {$mode ni [list "extended" "multiple"]} {
	return
    }

    set cellmode [expr {[$w cget -selecttype] eq "cell"}]
    if {$cellmode} {
	lassign [GetCurrentCell $w 0] current colId colNum
    } else {
	set current [GetCurrent $w 0]
    }
    if {$current eq ""} { return }

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
		$w cellselection set [list [TopItem $w] $colId] \
		    [list [BottomItem $w] $colId]
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
	    if {$cellmode} {
		$w cellselection set [list $current [FirstColumnId $w]] \
		    [list $current [LastColumnId $w]]
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
	heading	{ heading.release $w $x $y }
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
    set column [$w column [$w identify column $x $y] -id]
    array set State [list pressMode "heading" heading $column \
	cursor [$w cget -cursor] activeHeading ""]
    $w heading $column state pressed
}

proc ::ttk::treeview::heading.drag {w x y} {
    variable State
    if {[$w identify region $x $y] eq "heading"} {
	set column [$w column [$w identify column $x $y] -id]
	if {$column ne $State(heading)} {
	    if {$State(heading) ne "#0" && [$w cget -cursor] eq $State(cursor)} {
		set cursor [ttk::cursor move]
		set State(cursor) [$w cget -cursor]
		ttk::setCursor $w $cursor
	    }
	    set active $State(activeHeading)
	    if {$active ne $column} {
		if {$active ne ""} {
		    $w heading $active state !active
		}
		set State(activeHeading) $column
		$w heading $column state active
	    }
	}

    }
}

proc ::ttk::treeview::heading.release {w x y} {
    variable State
    set region [$w identify region $x $y]

    if {$region in [list "heading" "separator"]} {
	set column [$w column [$w identify column $x $y] -id]
	if {$region eq "heading" && $column eq $State(heading)} {
	    # Sort
	    after 0 [$w heading $State(heading) -command]
	} else {
	    # Move
	    set columns [$w cget -displaycolumns]
	    if {[llength $columns] == 1 && $columns eq "#all"} {
		set columns [$w cget -columns]
	    }
	    if {$region eq "separator"} {
		set index [lsearch $columns $column]
		set column [lindex $columns [incr index]]
		if {$column eq ""} {
		    set column "#end"
		}
	    }
	    if {$State(heading) ne "#0" && $column ne "#0"} {
		set index [lsearch $columns $State(heading)]
		set columns [lreplace $columns $index $index]
		if {$column ne "#end"} {
		    set index [lsearch $columns $column]
		    $w configure -displaycolumns [linsert $columns $index $State(heading)]
		} else {
		    $w configure -displaycolumns [linsert $columns end $State(heading)]
		}
	    }
	}
    }
    if {$State(activeHeading) ne ""} {
	$w heading $State(activeHeading) state !active
	set State(activeHeading) {}
    }
    $w heading $State(heading) state [list !active !pressed]
    if {[$w cget -cursor] ne $State(cursor)} {
	ttk::setCursor $w $State(cursor)
    }
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

    if {[$w cget -selecttype] eq "cell"} {
	set cell [$w cellfocus]
    } else {
	set cell ""
    }

    if {$op eq "toggle" && [$w haschildren $focus]} {
	Toggle $w $focus
    } else {
	SelectOp $w $focus $cell toggle
    }
}

#
# BrowseTo -- navigate to specified item; set focus and selection
#
proc ::ttk::treeview::BrowseTo {w item cell {op set}} {
    variable State

    if {$op ne "none"} {
	if {$cell ne ""} {
	    $w cellselection anchor $cell
	    $w cellselection $op [list $cell]
	    $w cellfocus $cell
	} else {
	    $w selection anchor $item
	    $w selection $op [list $item]
	    $w focus $item
	}
    } else {
	if {$cell ne ""} {
	    $w cellfocus $cell
	} else {
	    $w focus $item
	}
    }
    $w see $item
    array set State [list current $item currentCell $cell]
}

#
# ExtendTo -- Extend selection
#
proc ::ttk::treeview::ExtendTo {w item cell {op set}} {
    variable State

    if {$cell ne ""} {
	set anchor [$w cellselection anchor]
	if {$anchor ne ""} {
	    $w cellselection $op $anchor $cell
	    $w see $item
	} else {
	    BrowseTo $w $item $cell $op
	}
    } else {
	set anchor [$w selection anchor]
	if {$anchor ne ""} {
	    $w selection $op $anchor $item
	    $w see $item
	} else {
	    BrowseTo $w $item $cell $op
	}
    }
    array set State [list current $item currentCell $cell]
}

#
# Encode cell value into clipboard format
#
proc ::ttk::treeview::EncodeValue {string} {
    if {[string first "\t" $string] < 0 && [string first "\n" $string] < 0} {
	return $string
    } else {
	return [string cat "\"" [string map [list \" \"\"] $string] "\""]
    }
}

#
# Copy to clipboard
#
proc ::ttk::treeview::CopyToClipboard {w} {
    set data ""
    set format [expr {$::tcl_platform(platform) ne "windows" ? "UTF8_STRING" : "STRING"}]

    # Determine which columns are shown
    set columns [$w cget -displaycolumns]
    if {[llength $columns] == 1 && $columns eq "#all"} {
	set columns [$w cget -columns]
	set use_values 1
    } else {
	set use_values 0
    }

    # Get selected items or cells in display column order
    if {[$w cget -selecttype] eq "item"} {
	set inc_tree [expr {"tree" in [$w cget -show]}]

	foreach item [$w selection] {
	    set list [list]
	    if {$inc_tree} {
		lappend list [EncodeValue [$w item $item -text]]
	    }
	    if {$use_values} {
		foreach val [$w item $item -value] {
		    lappend list [EncodeValue $val]
		}
	    } else {
		foreach col $columns {
		    lappend list [EncodeValue [$w set $item $col]]
		}
	    }
	    append data [join $list "\t"] "\n"
	}
    } else {
	set prev ""
	set list [list]
	set temp [list]

	# Get selected cells by item and in display column order
	foreach cell [$w cellselection] {
	    lassign $cell item column
	    if {$column ne "#0"} {
		set column [format "#%d" [expr {[lsearch $columns $column] + 1}]]
	    }
	    if {$prev eq "" || $item eq $prev} {
		lappend temp $column
	    } else {
		lappend list $prev [lsort -dictionary $temp]
		set temp [list $column]
	    }
	    set prev $item
	}
	if {[llength $temp] > 0} {
	    lappend list $prev [lsort -dictionary $temp]
	}

	# Get cell value and append to data in clipboard format
	foreach {item cols} $list {
	    set temp [list]
	    foreach col $cols {
		lappend temp [EncodeValue [$w set $item $col]]
	    }
	    append data [join $temp "\t"] "\n"
	}
    }

    # Append data to clipboard
    clipboard clear -displayof $w
    clipboard append -displayof $w -format $format -type STRING -- $data
}

#*EOF*
