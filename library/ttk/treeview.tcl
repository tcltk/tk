#
# ttk::treeview widget bindings and utilities.
#

namespace eval ttk::treeview {
    variable State

    # Enter/Leave/Motion
    #
    set State(activeWidget) 	{}
    set State(activeHeading) 	{}

    # Press/drag/release:
    #
    set State(pressMode) 	none
    set State(pressX)		0

    # For pressMode == "resize"
    set State(resizeColumn)	#0

    # For pressmode == "heading"
    set State(heading)  	{}

    set State(cellAnchor)	{}
    set State(cellAnchorOp)	"set"
    set State(cellCurrent)	{}

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


### Widget bindings.
#

# Mouse button bindings
bind Treeview	<Motion> 		{ ::ttk::treeview::Motion %W %x %y }
bind Treeview	<B1-Leave>		{ #nothing }
bind Treeview	<Leave>			{ ::ttk::treeview::ActivateHeading {} {}}
bind Treeview	<Button-1> 		{ ::ttk::treeview::Press %W %x %y }
bind Treeview	<Double-Button-1> 	{ ::ttk::treeview::DoubleClick %W %x %y }
bind Treeview	<ButtonRelease-1> 	{ ::ttk::treeview::Release %W %x %y }
bind Treeview	<B1-Motion> 		{ ::ttk::treeview::Drag %W %x %y }

bind Treeview	<Shift-Button-1> 	{ ::ttk::treeview::Select %W %x %y extend }
bind Treeview	<<ToggleSelection>>	{ ::ttk::treeview::Select %W %x %y toggle }

# Arrow key navigation bindings
bind Treeview 	<<PrevChar>> 		{ ::ttk::treeview::KeyNav %W left }
bind Treeview 	<<NextChar>> 		{ ::ttk::treeview::KeyNav %W right }
bind Treeview 	<<PrevLine>>    	{ ::ttk::treeview::KeyNav %W up }
bind Treeview 	<<NextLine>>  		{ ::ttk::treeview::KeyNav %W down }
bind Treeview	<<PrevPara>>		{ ::ttk::treeview::KeyNav %W top }
bind Treeview	<<NextPara>>		{ ::ttk::treeview::KeyNav %W bottom }

# Selection toggle bindings
bind Treeview	<Return>		{ ::ttk::treeview::ToggleFocus %W }
bind Treeview	<space>			{ ::ttk::treeview::ToggleFocus %W }

# Home and End bindings
bind Treeview	<<LineStart>>		{ ::ttk::treeview::KeyNav %W first }
bind Treeview	<<LineEnd>>		{ ::ttk::treeview::KeyNav %W last }
bind Treeview	<Control-Home>		{ ::ttk::treeview::KeyNav %W top }
bind Treeview	<Control-End>		{ ::ttk::treeview::KeyNav %W bottom }

# Page up/down bindings
bind Treeview	<Prior>			{ ::ttk::treeview::ScrollPage %W up }
bind Treeview	<Next> 			{ ::ttk::treeview::ScrollPage %W down }
bind Treeview	<Control-Prior>		{ ::ttk::treeview::ScrollPage %W top }
bind Treeview	<Control-Next> 		{ ::ttk::treeview::ScrollPage %W bottom }

# Page left/right bindings
bind Treeview	<<PrevWord>>		{ ::ttk::treeview::ScrollPage %W left }
bind Treeview	<<NextWord>>		{ ::ttk::treeview::ScrollPage %W right }

# Selection extend bindings
bind Treeview	<<SelectPrevChar>>	{ ::ttk::treeview::SelectionExtend %W left }
bind Treeview	<<SelectNextChar>>	{ ::ttk::treeview::SelectionExtend %W right }
bind Treeview	<<SelectPrevLine>>	{ ::ttk::treeview::SelectionExtend %W up }
bind Treeview	<<SelectNextLine>>	{ ::ttk::treeview::SelectionExtend %W down }
bind Treeview	<<SelectPrevWord>>	{ ::ttk::treeview::SelectionExtend %W first }
bind Treeview	<<SelectNextWord>>	{ ::ttk::treeview::SelectionExtend %W last }
bind Treeview	<<SelectLineStart>>	{ ::ttk::treeview::SelectionExtend %W first }
bind Treeview	<<SelectLineEnd>>	{ ::ttk::treeview::SelectionExtend %W last }
bind Treeview	<<SelectPrevPara>>	{ ::ttk::treeview::SelectionExtend %W top }
bind Treeview	<<SelectNextPara>>	{ ::ttk::treeview::SelectionExtend %W bottom }
bind Treeview	<Control-Shift-Home>	{ ::ttk::treeview::SelectionExtend %W top }
bind Treeview	<Control-Shift-End>	{ ::ttk::treeview::SelectionExtend %W bottom }

# Other selection functions
bind Treeview	<<SelectAll>>		{ ::ttk::treeview::SelectAll %W }
bind Treeview	<<SelectNone>>		{ ::ttk::treeview::SelectNone %W }

# Mousewheel and TouchpadScroll
ttk::copyBindings TtkScrollable Treeview

### Binding procedures.
#

# Get first column number
proc ::ttk::treeview::FirstColumn {w} {
    return [expr {"tree" ni [$w cget -show]}]
}

# Get last column number
proc ::ttk::treeview::LastColumn {w} {
    set columns [$w cget -displaycolumns]
    if {$columns eq "#all"} {
	set columns [$w cget -columns]
    }
    return [llength $columns]
}

# Get current column number
proc ::ttk::treeview::GetColumn {w} {
    variable State
    lassign $State(cellAnchor) anchor colAnchor
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

## KeyNav -- Keyboard navigation to move focus and selected item/cell
#
proc ::ttk::treeview::KeyNav {w dir} {
    set focus [$w focus]
    if {$focus eq ""} { return }

    set cellmode [expr {[$w cget -selecttype] eq "cell"}]
    if {$cellmode} {
	set colNum [GetColumn $w]
    }

    switch -- $dir {
	up {
	    # Move up one item/cell
	    if {[set up [$w prev $focus]] eq ""} {
	        set focus [$w parent $focus]
	    } else {
		while {[$w item $up -open] && [$w haschildren $up]} {
		    set up [$w id $up end]
		}
		set focus $up
	    }
	}
	down {
	    # Move down one item/cell
	    if {[$w item $focus -open] && [$w haschildren $focus]} {
	        set focus [$w id $focus first]
	    } else {
		set up $focus
		while {$up ne "" && [set down [$w next $up]] eq ""} {
		    set up [$w parent $up]
		}
		set focus $down
	    }
	}
	left {
	    # Move one cell left or close item if open
	    if {$cellmode} {
		if {$colNum > [FirstColumn $w]} {
		    incr colNum -1
		}
	    } elseif {[$w item $focus -open] && [$w haschildren $focus]} {
	    	CloseItem $w $focus
	    } else {
	    	set focus [$w parent $focus]
	    }
	}
	right {
	    # Move one cell right or open item if closed
	    if {$cellmode} {
		# Move to next cell
		if {$colNum < [LastColumn $w]} {
		    incr colNum
		}
	    } elseif {![$w item $focus -open] && [$w haschildren $focus]} {
	    	OpenItem $w $focus
	    }
	}
	first {
	    # Move to first cell in item or first child in parent
	    if {$cellmode} {
		set colNum [FirstColumn $w]
	    } else {
		set focus [$w id [$w parent $focus] first]
	    }
	}
	last {
	    # Move to last cell in item or last child in parent
	    if {$cellmode} {
		set colNum [LastColumn $w]
	    } else {
		set focus [$w id [$w parent $focus] last]
	    }
	}
	top {
	    # Move to beginning and select first item or cell in col
	    $w yview moveto 0
	    set focus [$w id {} first]
	}
	bottom {
	    # Move to end and select last item or cell in col
	    $w yview moveto 1
	    set focus [$w id {} last]
	    while {[$w item $focus -open] && [$w haschildren $focus]} {
		set focus [$w id $focus last]
	    }
	}
    }

    # Do select
    if {$focus ne {}} {
	if {$cellmode} {
	    SelectOp $w $focus [list $focus [format "#%d" $colNum]] choose
	} else {
	    SelectOp $w $focus "" move
	}
    }
}

## ScrollPage -- Scroll view and move focus/selected item
#
proc ::ttk::treeview::ScrollPage {w dir} {
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
	incr x [expr {$width / 2}]
	incr y [expr {$height / 2}]
    }

    switch -- $dir {
	up {
	    # Scroll up
	    lassign [$w yview] start end
	    if {$start > 0.0} {
		# Scroll up 1 page and select item/cell at same coord
		$w yview scroll -1 pages
		update idletasks
		set focus [$w identify item $x $y]
	    } else {
		# Select first item/cell if at top already
		set focus [$w id {} first]
	    }
	}
	down {
	    # Scroll down
	    lassign [$w yview] start end
	    if {$end < 1.0} {
		# Scroll down 1 page and select item/cell at same coord
		$w yview scroll 1 pages
		update idletasks
		set focus [$w identify item $x $y]
	    } else {
		# Select last item/cell if at bottom already
		set focus [$w id {} last]
		while {[$w haschildren $focus] && [$w item $focus -open]} {
		    set focus [$w id $focus last]
		}
	    }
	}
	left {
	    # Scroll left 1 page
	    lassign [$w xview] start end
	    if {$start > 0.0} {
		$w xview scroll -1 pages
		update idletasks
		if {$cellmode} {
		    # Select cell at same coord
		    lassign [$w identify cell $x $y] focus colId
		}
	    } else {
		if {$cellmode} {
		    # Select first cell if at left edge already
		    set colId [format "#%0d" [FirstColumn $w]]
		}
	    }
	}
	right {
	    # Scroll right 1 page
	    lassign [$w xview] start end
	    if {$end < 1.0} {
		$w xview scroll 1 pages
		update idletasks
		if {$cellmode} {
		    # Select cell at same coord
		    lassign [$w identify cell $x $y] focus colId
		}
	    } else {
		if {$cellmode} {
		    # Select last cell if at right edge already
		    set colId [format "#%0d" [LastColumn $w]]
		}
	    }
	}
	first {
	    # Move to left edge & select first cell in item if in cell mode
	    $w xview moveto 0
	    if {$cellmode} {
		set colId [format "#%0d" [FirstColumn $w]]
	    }
	}
	last {
	    # Move to right edge & select last cell in item if in cell mode
	    $w xview moveto 1
	    if {$cellmode} {
		set colId [format "#%0d" [LastColumn $w]]
	    }
	}
	top {
	    # Move to beginning and select first item or cell in col
	    $w yview moveto 0
	    set focus [$w id {} first]
	}
	bottom {
	    # Move to end and select last item or cell in col
	    $w yview moveto 1
	    set focus [$w id {} last]
	    while {[$w item $focus -open] && [$w haschildren $focus]} {
		set focus [$w id $focus last]
	    }
	}
    }

    # Do select
    if {$focus ne ""} {
	if {$cellmode} {
	    SelectOp $w $focus [list $focus $colId] choose
	} else {
	    SelectOp $w $focus "" move
	}
    }
}

## SelectionExtend -- Extend item/cell selection
#
proc ::ttk::treeview::SelectionExtend {w dir} {
    variable State
    set focus [$w focus]

    if {[$w cget -selectmode] ni [list "extended" "multiple"] || $focus eq ""} {
	return
    }

    set cellmode [expr {[$w cget -selecttype] eq "cell"}]
    if {$cellmode} {
	set colNum [GetColumn $w]
    }

    lassign $State(cellCurrent) current colCurrent
    if {$focus ne $current} {
	set focus $current
	scan $colCurrent "#%d" colNum
    }

    switch -- $dir {
	up {
	    # Extend selection to prev item
	    if {[set up [$w prev $focus]] eq ""} {
	        set focus [$w parent $focus]
	    } else {
		while {[$w item $up -open] && [$w haschildren $up]} {
		    set up [$w id $up end]
		}
		set focus $up
	    }
	}
	down {
	    # Extend selection to next item
	    if {[$w item $focus -open] && [$w haschildren $focus]} {
	        set focus [$w id $focus first]
	    } else {
		set up $focus
		while {$up ne "" && [set down [$w next $up]] eq ""} {
		    set up [$w parent $up]
		}
		set focus $down
	    }
	}
	left {
	    # Extend selection to prev cell
	    if {$cellmode} {
		if {$colNum > [FirstColumn $w]} {
		    incr colNum -1
		}
	    }
	}
	right {
	    # Extend selection to next cell
	    if {$cellmode} {
		if {$colNum < [LastColumn $w]} {
		    incr colNum
		}
	    }
	}
	first {
	    # Select all cells from current cell to first cell in same item
	    # or items from focus to parent's first item
	    if {$cellmode} {
		set colNum [FirstColumn $w]
	    } else {
		set focus [$w id [$w parent $focus] first]
	    }
	}
	last {
	    # Select all cells from current cell to last cell in same item
	    # or items from focus to parent's last item
	    if {$cellmode} {
		set colNum [LastColumn $w]
	    } else {
		set focus [$w id [$w parent $focus] last]
	    }
	}
	top {
	    # Select all cells/items from focus to top
	    set focus [$w id {} first]
	}
	bottom {
	    # Select all cells/items from focus to bottom
	    set focus [$w id {} last]
	    while {[$w item $focus -open] && [$w haschildren $focus]} {
		set focus [$w id $focus last]
	    }
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

## SelectAll -- select all items/cells under item
#
proc ::ttk::treeview::SelectAll {w {item {}}} {
    set cellmode [expr {[$w cget -selecttype] eq "cell"}]
    if {$cellmode} {
	SelectAllCells $w $item
    } else {
	SelectAllItems $w $item
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

proc ::ttk::treeview::SelectAllCells {w item} {
    set first [format "#%d" [FirstColumn $w]]
    set last  [format "#%d" [LastColumn $w]]

    if {$item eq "" || ([$w item $item -open] && [$w haschildren $item])} {
	foreach child [$w children $item] {
	    $w cellselection add [list $child $first] [list $child $last]
	    SelectAllCells $w $child
	}
    }
}

## SelectNone -- Unselect all items/cells
#
proc ::ttk::treeview::SelectNone {w} {
    set cellmode [expr {[$w cget -selecttype] eq "cell"}]
    if {$cellmode} {
	$w cellselection set {}
    } else {
	$w selection set {}
    }
}

## Motion -- pointer motion binding.
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

## IndentifyCell -- Locate the cell at coordinate
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

## Select $w $x $y $selectop
#	Binding procedure for selection operations.
#	See "Selection modes", below.
#
proc ::ttk::treeview::Select {w x y op} {
    if {[set item [$w identify row $x $y]] ne "" } {
	set cell [IdentifyCell $w $x $y]
	SelectOp $w $item $cell $op
    }
}

## DoubleClick -- Double-Button-1 binding.
#
proc ::ttk::treeview::DoubleClick {w x y} {
    if {[set row [$w identify row $x $y]] ne ""} {
	Toggle $w $row
    } else {
	Press $w $x $y ;# perform single-click action
    }
}

## Press -- Button binding.
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

## Drag -- B1-Motion binding
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

### Interactive column resizing.
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

### Heading activation.
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

### Selection modes.
#

## SelectOp $w $item $cell [ move | choose | extend | toggle ] --
#	Dispatch to appropriate selection operation
#	depending on current value of -selectmode.
#
proc ::ttk::treeview::SelectOp {w item cell op} {
    select.$op.[$w cget -selectmode] $w $item $cell
}

## -selectmode none:
#
proc ::ttk::treeview::select.move.none   {w item cell} { $w focus $item; $w see $item }
proc ::ttk::treeview::select.choose.none {w item cell} { $w focus $item; $w see $item }
proc ::ttk::treeview::select.toggle.none {w item cell} { $w focus $item; $w see $item }
proc ::ttk::treeview::select.extend.none {w item cell} { $w focus $item; $w see $item }

## -selectmode single:
#
proc ::ttk::treeview::select.move.single   {w item cell} { BrowseTo $w $item $cell }
proc ::ttk::treeview::select.choose.single {w item cell} { BrowseTo $w $item $cell }
proc ::ttk::treeview::select.toggle.single {w item cell} { BrowseTo $w $item $cell }
proc ::ttk::treeview::select.extend.single {w item cell} { BrowseTo $w $item $cell }

## -selectmode browse:
#
proc ::ttk::treeview::select.move.browse   {w item cell} { BrowseTo $w $item $cell }
proc ::ttk::treeview::select.choose.browse {w item cell} { BrowseTo $w $item $cell }
proc ::ttk::treeview::select.toggle.browse {w item cell} { BrowseTo $w $item $cell }
proc ::ttk::treeview::select.extend.browse {w item cell} { BrowseTo $w $item $cell }

## -selectmode extended:
#
proc ::ttk::treeview::select.move.extended {w item cell}   { BrowseTo $w $item $cell }
proc ::ttk::treeview::select.choose.extended {w item cell} { BrowseTo $w $item $cell }
proc ::ttk::treeview::select.toggle.extended {w item cell} {
    variable State
    if {$cell ne ""} {
	$w cellselection toggle [list $cell]
	set State(cellAnchor) $cell
	set State(cellAnchorOp) add
	set State(cellCurrent) $cell
    } else {
	$w selection toggle [list $item]
    }
}
proc ::ttk::treeview::select.extend.extended {w item cell} {
    variable State
    if {$cell ne ""} {
	set State(cellCurrent) $cell
	if {$State(cellAnchor) ne ""} {
	    $w cellselection $State(cellAnchorOp) $State(cellAnchor) $cell
	} else {
	    BrowseTo $w $item $cell
	}
    } else {
	if {[set anchor [$w focus]] ne ""} {
	    $w selection set [between $w $anchor $item]
	} else {
	    BrowseTo $w $item $cell
	}
    }
}

## -selectmode multiple:
#
proc ::ttk::treeview::select.move.multiple {w item cell} {
    variable State
    if {$cell ne ""} {
    } else {
	$w focus $item
    }
}
proc ::ttk::treeview::select.choose.multiple {w item cell} {
    variable State
    if {$cell ne ""} {
	$w cellselection toggle [list $cell]
	set State(cellAnchor) $cell
	set State(cellAnchorOp) add
	set State(cellCurrent) $cell
    } else {
	$w focus $item
	$w selection toggle [list $item]
    }
}
proc ::ttk::treeview::select.toggle.multiple {w item cell} {
    variable State
    if {$cell ne ""} {
	$w cellselection toggle [list $cell]
	set State(cellAnchor) $cell
	set State(cellAnchorOp) add
	set State(cellCurrent) $cell
    } else {
	$w focus $item
	$w selection toggle [list $item]
    }
}
proc ::ttk::treeview::select.extend.multiple {w item cell} {
    variable State
    if {$cell ne ""} {
	$w cellselection toggle [list $cell]
	set State(cellAnchor) $cell
	set State(cellAnchorOp) add
	set State(cellCurrent) $cell
    } else {
	$w focus $item
	$w selection toggle [list $item]
    }
}

### Tree structure utilities.
#

## between $tv $item1 $item2 --
#	Returns a list of all items between $item1 and $item2,
#	in preorder traversal order.  $item1 and $item2 may be
#	in either order.
#
# NOTES:
#	This routine is O(N) in the size of the tree.
#	There's probably a way to do this that's O(N) in the number
#	of items returned, but I'm not clever enough to figure it out.
#
proc ::ttk::treeview::between {tv item1 item2} {
    variable between [list]
    variable selectingBetween 0
    ScanBetween $tv $item1 $item2 {}
    return $between
}

## ScanBetween --
#	Recursive worker routine for ttk::treeview::between
#
proc ::ttk::treeview::ScanBetween {tv item1 item2 item} {
    variable between
    variable selectingBetween

    if {$item eq $item1 || $item eq $item2} {
	lappend between $item
	set selectingBetween [expr {!$selectingBetween}]
    } elseif {$selectingBetween} {
	lappend between $item
    }
    foreach child [$tv children $item] {
	ScanBetween $tv $item1 $item2 $child
    }
}

### User interaction utilities.
#

## OpenItem, CloseItem -- Set the open state of an item, generate event
#

proc ::ttk::treeview::OpenItem {w item} {
    $w focus $item
    event generate $w <<TreeviewOpen>>
    $w item $item -open true
}

proc ::ttk::treeview::CloseItem {w item} {
    $w item $item -open false
    $w focus $item
    event generate $w <<TreeviewClose>>
}

## Toggle -- toggle opened/closed state of item
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

## ToggleFocus -- toggle opened/closed state of focus item
#
proc ::ttk::treeview::ToggleFocus {w} {
    set item [$w focus]
    if {$item ne ""} {
	if {[$w cget -selectmode] eq "multiple"} {
	    SelectOp $w $item {} choose
	} else {
	    Toggle $w $item
	}
    }
}

## BrowseTo -- navigate to specified item; set focus and selection
#
proc ::ttk::treeview::BrowseTo {w item cell} {
    variable State

    $w see $item
    $w focus $item
    set State(cellAnchor) $cell
    set State(cellAnchorOp) set
    set State(cellCurrent) $cell
    if {$cell ne ""} {
	$w cellselection set [list $cell]
    } else {
	$w selection set [list $item]
    }
}

#*EOF*
