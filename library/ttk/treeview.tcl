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
    set State(moved)		0

    # For pressMode eq "resize"
    set State(resizeColumn)	#0

    # For pressMode eq "heading"
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

if {$::tcl_platform(os) ne "Darwin"} {
    set ckey Control
} else {
    set ckey Option
}


#
# Widget bindings.
#

# Clipboard and selection functions
bind Treeview	<<Copy>>		{ ::ttk::treeview::CopyToClipboard %W }

# Mouse button bindings
bind Treeview	<Motion>		{ ::ttk::treeview::Motion %W %x %y }
bind Treeview	<B1-Leave>		{}
bind Treeview	<Leave>			{ ::ttk::treeview::ActivateHeading {} {}}
bind Treeview	<Button-1>		{ ::ttk::treeview::Press %W %x %y }
bind Treeview	<Double-Button-1>	{ ::ttk::treeview::DoubleClick %W %x %y }
bind Treeview	<B1-Motion>		{ ::ttk::treeview::Drag %W %x %y }
bind Treeview	<ButtonRelease-1>	{ ::ttk::treeview::Release %W %x %y }
bind Treeview	<Shift-Button-1>	{ ::ttk::treeview::MouseSelect %W %x %y extend }
bind Treeview	<<ToggleSelection>>	{ ::ttk::treeview::MouseSelect %W %x %y toggle }

# Left/Right arrow key bindings (none, shift, control, control+shift)
bind Treeview	<<PrevChar>>		{ ::ttk::treeview::KeyNav %W left }
bind Treeview	<<NextChar>>		{ ::ttk::treeview::KeyNav %W right }
bind Treeview	<<SelectPrevChar>>	{ ::ttk::treeview::KeyNav %W left extend }
bind Treeview	<<SelectNextChar>>	{ ::ttk::treeview::KeyNav %W right extend }
bind Treeview	<<PrevWord>>		{ ::ttk::treeview::KeyNav %W pageLeft }
bind Treeview	<<NextWord>>		{ ::ttk::treeview::KeyNav %W pageRight }
bind Treeview	<<SelectPrevWord>>	{ ::ttk::treeview::KeyNav %W pageLeft extend }
bind Treeview	<<SelectNextWord>>	{ ::ttk::treeview::KeyNav %W pageRight extend }

# Up/down arrow key bindings (none, shift, control, control+shift)
bind Treeview	<<PrevLine>>		{ ::ttk::treeview::KeyNav %W up }
bind Treeview	<<NextLine>>		{ ::ttk::treeview::KeyNav %W down }
bind Treeview	<<SelectPrevLine>>	{ ::ttk::treeview::KeyNav %W up extend }
bind Treeview	<<SelectNextLine>>	{ ::ttk::treeview::KeyNav %W down extend }
bind Treeview	<<PrevPara>>		{ ::ttk::treeview::KeyNav %W pageTop }
bind Treeview	<<NextPara>>		{ ::ttk::treeview::KeyNav %W pageBottom }
bind Treeview	<<SelectPrevPara>>	{ ::ttk::treeview::KeyNav %W pageTop extend }
bind Treeview	<<SelectNextPara>>	{ ::ttk::treeview::KeyNav %W pageBottom extend }

# Home/End key bindings (none, shift, control, control+shift)
bind Treeview	<<LineStart>>		{ ::ttk::treeview::KeyNav %W first }
bind Treeview	<<LineEnd>>		{ ::ttk::treeview::KeyNav %W last }
bind Treeview	<<SelectLineStart>>	{ ::ttk::treeview::KeyNav %W first extend }
bind Treeview	<<SelectLineEnd>>	{ ::ttk::treeview::KeyNav %W last extend }
bind Treeview	<${ckey}-Home>		{ ::ttk::treeview::KeyNav %W topleft }
bind Treeview	<${ckey}-End>		{ ::ttk::treeview::KeyNav %W bottomright }
bind Treeview	<${ckey}-Shift-Home>	{ ::ttk::treeview::KeyNav %W topleft extend }
bind Treeview	<${ckey}-Shift-End>	{ ::ttk::treeview::KeyNav %W bottomright extend }

# Page Up/Down key bindings (none, shift, control, control+shift, alternate)
bind Treeview	<Prior>			{ ::ttk::treeview::PageNav %W up }
bind Treeview	<Next>			{ ::ttk::treeview::PageNav %W down }
bind Treeview	<Shift-Prior>		{ ::ttk::treeview::PageNav %W left }
bind Treeview	<Shift-Next>		{ ::ttk::treeview::PageNav %W right }
bind Treeview	<${ckey}-Prior>		{ ::ttk::treeview::PageNav %W pageTop }
bind Treeview	<${ckey}-Next>		{ ::ttk::treeview::PageNav %W pageBottom }
bind Treeview	<${ckey}-Shift-Prior>	{ ::ttk::treeview::PageNav %W pageLeft }
bind Treeview	<${ckey}-Shift-Next>	{ ::ttk::treeview::PageNav %W pageRight }
bind Treeview	<Alt-Prior>		{ ::ttk::treeview::PageNav %W left; break }
bind Treeview	<Alt-Next>		{ ::ttk::treeview::PageNav %W right; break }

# Scroll Lock bindings
if {$::tcl_platform(os) ne "Darwin"} {
bind Treeview	<Mod3-Up>		{ %W yview scroll -1 units }
bind Treeview	<Mod3-Down>		{ %W yview scroll 1 units }
bind Treeview	<Mod3-Left>		{ %W xview scroll -10 units }
bind Treeview	<Mod3-Right>		{ %W xview scroll 10 units }
bind Treeview	<Mod3-Prior>		{ %W yview scroll -1 pages }
bind Treeview	<Mod3-Next>		{ %W yview scroll 1 pages }
bind Treeview	<Mod3-${ckey}-Prior>	{ %W yview moveto 0.0 }
bind Treeview	<Mod3-${ckey}-Next>	{ %W yview moveto 1.0 }
bind Treeview	<Mod3-Home>		{ %W xview scroll -1 pages }
bind Treeview	<Mod3-End>		{ %W xview scroll 1 pages }
bind Treeview	<Mod3-${ckey}-Home>	{ %W xview moveto 0.0 }
bind Treeview	<Mod3-${ckey}-End>	{ %W xview moveto 1.0 }
}

# Other keys
bind Treeview	<F2>			{ ::ttk::treeview::ActivateItem %W }
bind Treeview	<Return>		{ ::ttk::treeview::ActivateItem %W }
bind Treeview	<Shift-Return>		{ ::ttk::treeview::KeyNav %W up }
bind Treeview	<${ckey}-Return>	{ ::ttk::treeview::ActivateItem %W }
bind Treeview	<<Invoke>>		{ ::ttk::treeview::ActivateItem %W }

bind Treeview	<space>			{ ::ttk::treeview::ToggleSelected %W select }
bind Treeview	<Shift-space>		{ ::ttk::treeview::SelectionSet %W row }
bind Treeview	<${ckey}-space>		{ ::ttk::treeview::SelectionSet %W column }
bind Treeview	<${ckey}-Shift-space>	{ ::ttk::treeview::SelectionSet %W all }

bind Treeview	<Tab>			{ ::ttk::treeview::KeyNav %W right; break }
bind Treeview	<Shift-Tab>		{ ::ttk::treeview::KeyNav %W left; break }
bind Treeview	<${ckey}-Tab>		[bind all <<NextWindow>>]
bind Treeview	<${ckey}-Shift-Tab>	[bind all <<PrevWindow>>]

# Other selection functions
bind Treeview	<<SelectAll>>		{ ::ttk::treeview::SelectionSet %W all }
bind Treeview	<<SelectInvert>>	{ ::ttk::treeview::SelectionSet %W invert }
bind Treeview	<<SelectNone>>		{ ::ttk::treeview::SelectNone %W }
bind Treeview	<minus>			{ ::ttk::treeview::CloseItem %W {} }
bind Treeview	<plus>			{ ::ttk::treeview::OpenItem %W {} }
bind Treeview	<asterisk>		{ ::ttk::treeview::OpenItem %W {} -recurse }
unset ckey

# Mousewheel and TouchpadScroll
ttk::copyBindings TtkScrollable Treeview

#
# Binding procedures.
#

#
# Get first column number or id
#
proc ::ttk::treeview::FirstColumnNum {w} {
    return [expr {"tree" ni [$w cget -show]}]
}
proc ::ttk::treeview::FirstColumnId {w} {
    return [format "#%d" [FirstColumnNum $w]]
}

#
# Get last column number or id
#
proc ::ttk::treeview::LastColumnNum {w} {
    set columns [$w cget -displaycolumns]
    if {[lindex $columns 0] eq "#all"} {
	set columns [$w cget -columns]
    }
    return [llength $columns]
}
proc ::ttk::treeview::LastColumnId {w} {
    return [format "#%d" [LastColumnNum $w]]
}

#
# Get current item
#
proc ::ttk::treeview::GetCurrentItem {w skip} {
    variable State
    set focus [$w focus]

    if {$State(current) ne "" && !$skip} {
	set item $State(current)
    } elseif {$focus ne ""} {
	set item $focus
    } else {
	set item [$w identifier {} 0]
    }
    return $item
}

#
# Get current cell
#
proc ::ttk::treeview::GetCurrentCell {w skip} {
    variable State
    lassign [$w cellfocus] focus colFocus

    if {[llength $State(currentCell)] == 2 && !$skip} {
	lassign $State(currentCell) item column
    } elseif {$focus ne "" && $colFocus ne ""} {
	set item $focus
	set column $colFocus
    } else {
	set item [$w identifier {} 0]
	set column [FirstColumnId $w]
    }

    # Convert to display column number
    if {[string index $column 0] eq "#"} {
	scan $column "#%d" colNum
    } elseif {[string is integer $column]} {
	set colNum $column
	incr colNum
    } else {
	set list [$w cget -displaycolumns]
	if {[lindex $list 0] eq "#all"} {
	    set list [$w cget -columns]
	}
	set colNum [lsearch $list $column]
	incr colNum
    }
    return [list $item $column $colNum]
}

#
# Get top-most fully visible item
#
proc ::ttk::treeview::PageTop {w} {
    set offset [expr {"headings" in [$w cget -show] ? [$w cget -headingheight] : 0}]
    incr offset [expr {[ttk::style configure Treeview -rowheight] * [$w cget -titleitems]}]
    return [$w identify item 10 [expr {$offset + 5}]]
}

#
# Get bottom-most fully visible item
#
proc ::ttk::treeview::PageBottom {w} {
    set item ""
    set rh [expr {[ttk::style configure Treeview -rowheight] * -1}]
    for {set y [expr {[winfo height $w] + $rh + 5}]} {$y > 0} {incr y $rh} {
	set item [$w identify item 10 $y]
	if {$item eq ""} continue
	lassign [$w bbox $item] x y wd ht
	if {$y + $ht <= [winfo height $w]} {
	    break
	}
    }
    return $item
}

#
# Get first item in widget
#
proc ::ttk::treeview::GetFirstItem {w} {
    set item [$w identifier {} first]
    if {[$w item $item -hidden]} {
	set item [$w after $item]
    }
    return $item
}

#
# Get last item in widget
#
proc ::ttk::treeview::GetLastItem {w} {
    set item [$w identifier {} last]
    while {[$w item $item -open] && [$w haschildren $item]} {
	set item [$w identifier $item last]
    }
    if {[$w item $item -hidden]} {
	set item [$w before $item]
    }
    return $item
}

#
# Get widget width
#
proc ::ttk::treeview::GetWidth {w} {
    # Get widget width
    set width 0
    set list [list]
    for {set i [FirstColumnNum $w]} {$i <= [LastColumnNum $w]} {incr i} {
	lappend list $i $width [incr width [$w column [format "#%d" $i] -width]]
    }
    return [list $list $width]
}

#
# Get left-most fully visible column
#
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

#
# Get right-most fully visible column
#
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
# op = moveto or extend
#
proc ::ttk::treeview::KeyNav {w fn {op moveto}} {
    if {[$w instate disabled]} return

    # Get current item and cell
    set cellmode [expr {[$w cget -selecttype] eq "cell"}]
    set unsel [expr {$op eq "moveto" ? 1 : 0}]
    if {$cellmode} {
	lassign [GetCurrentCell $w $unsel] item column colNum
    } else {
	set item [GetCurrentItem $w $unsel]
    }
    if {$item eq ""} {return}

    switch -- $fn {
	left {
	    # Move/extend left one cell or close item if open
	    if {$cellmode} {
		if {$colNum > [FirstColumnNum $w]} {
		    incr colNum -1
		}
	    } elseif {$op eq "extend"} {
	    } elseif {[$w item $item -open] && [$w haschildren $item]} {
		CloseItem $w $item
	    } else {
		set item [$w parent $item]
	    }
	}
	right {
	    # Move/extend right one cell or open item if closed
	    if {$cellmode} {
		if {$colNum < [LastColumnNum $w]} {
		    incr colNum
		}
	    } elseif {![$w item $item -open] && [$w haschildren $item]} {
		OpenItem $w $item
	    }
	}
	up {
	    # Move/extend up one item/cell
	    set item [$w before $item]
	}
	down {
	    # Move/extend down one item/cell
	    set item [$w after $item]
	}
	first {
	    # Move to first cell in item or first child in parent
	    if {$cellmode} {
		set colNum [FirstColumnNum $w]
	    } else {
		set item [$w id [$w parent $item] first]
	    }
	}
	last {
	    # Move to last cell in item or last child in parent
	    if {$cellmode} {
		set colNum [LastColumnNum $w]
	    } else {
		set item [$w id [$w parent $item] last]
	    }
	}
	top {
	    # Move/extend to top item in tree
	    set item [GetFirstItem $w]
	}
	bottom {
	    # Move/extend to bottom item in tree
	    set item [GetLastItem $w]
	}
	topleft {
	    # Move/extend to tree top-left cell
	    if {$cellmode} {
		set colNum [FirstColumnNum $w]
	    }
	    set item [GetFirstItem $w]
	}
	bottomright {
	    # Move/extend to tree bottom-right cell
	    if {$cellmode} {
		set colNum [LastColumnNum $w]
	    }
	    set item [GetLastItem $w]
	}
	pageLeft {
	    # Move/extend to left most visible column
	    if {$cellmode} {
		set colNum [PageLeft $w]
	    }
	}
	pageRight {
	    # Move/extend to right most visible column
	    if {$cellmode} {
		set colNum [PageRight $w]
	    }
	}
	pageTop {
	    # Move/extend to top most visible item
	    set item [PageTop $w]
	}
	pageBottom {
	    # Move/extend to bottom most visible item
	    set item [PageBottom $w]
	}
    }

    # Do select
    if {$item ne ""} {
	if {$cellmode} {
	    SelectOp $w $item [list $item [format "#%d" $colNum]] $op
	} else {
	    SelectOp $w $item "" $op
	}
    }
}

#
# PageNav -- Scroll view and move focus/selected item
#
proc ::ttk::treeview::PageNav {w fn} {
    if {[$w instate disabled]} return

    # Get current item and cell
    set cellmode [expr {[$w cget -selecttype] eq "cell"}]
    if {$cellmode} {
	lassign [GetCurrentCell $w 1] item column colNum
    } else {
	set item [GetCurrentItem $w 1]
    }
    if {$item eq ""} {return}

    if {$fn in [list down left right up]} {
	if {$cellmode} {
	    lassign [$w bbox $item $column] x y width height
	} else {
	    lassign [$w bbox $item] x y width height
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
		    # Select cell at same coordinate
		    lassign [$w identify cell $x $y] item column
		}
	    } else {
		if {$cellmode} {
		    # Select first cell if at left edge already
		    set column [FirstColumnId $w]
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
		    # Select cell at same coordinate
		    lassign [$w identify cell $x $y] item column
		}
	    } else {
		if {$cellmode} {
		    # Select last cell if at right edge already
		    set column [LastColumnId $w]
		}
	    }
	}
	up {
	    # Move up 1 page & select cell
	    lassign [$w yview] start end
	    if {$start > 0.0} {
		# Scroll up 1 page and select item/cell at same coordinate
		$w yview scroll -1 pages
		update idletasks
		set item [$w identify item $x $y]
	    } else {
		# Select topmost item/cell if at top already
		set item [$w id {} first]
	    }
	}
	down {
	    # Move down 1 page & select cell
	    lassign [$w yview] start end
	    if {$end < 1.0} {
		# Scroll down 1 page and select item/cell at same coordinate
		$w yview scroll 1 pages
		update idletasks
		set item [$w identify item $x $y]
	    } else {
		# Select bottom-most item/cell if at bottom already
		set item [$w id {} last]
		while {[$w haschildren $item] && [$w item $item -open]} {
		    set item [$w id $item last]
		}
	    }
	}
	first {
	    # Move to first cell in item or first child in parent
	    if {$cellmode} {
		set column [FirstColumnId $w]
	    } else {
		set item [$w id [$w parent $item] first]
	    }
	}
	last {
	    # Move to last cell in item or last child in parent
	    if {$cellmode} {
		set column [LastColumnId $w]
	    } else {
		set item [$w id [$w parent $item] last]
	    }
	}
	top {
	    # Move to & select topmost item/cell
	    $w yview moveto 0
	    set item [GetFirstItem $w]
	}
	bottom {
	    # Move to & select bottom-most item/cell
	    $w yview moveto 1
	    set item [GetLastItem $w]
	}
	pageLeft {
	    # Move to & select leftmost item/cell in current screen view
	    if {$cellmode} {
		set column [format "#%d" [PageLeft $w]]
	    }
	}
	pageRight {
	    # Move to & select rightmost item/cell in current screen view
	    if {$cellmode} {
		set column [format "#%d" [PageRight $w]]
	    }
	}
	pageTop {
	    # Move to & select topmost item/cell in current screen view
	    set item [PageTop $w]
	}
	pageBottom {
	    # Move to & select bottommost item/cell in current screen view
	    set item [PageBottom $w]
	}
    }

    # Do select
    if {$item ne ""} {
	if {$cellmode} {
	    SelectOp $w $item [list $item $column] moveto
	} else {
	    SelectOp $w $item "" moveto
	}
    }
}

#
# SelectionSet -- Set special selection types
#
proc ::ttk::treeview::SelectionSet {w fn} {
    if {[$w instate disabled]} return

    set mode [$w cget -selectmode]
    if {$mode ni [list "extended" "multiple"]} {
	return
    }

    # Get current item and cell
    set cellmode [expr {[$w cget -selecttype] eq "cell"}]
    if {$cellmode} {
	lassign [GetCurrentCell $w 1] item column colNum
    } else {
	set item [GetCurrentItem $w 1]
    }
    if {$item eq "" && $fn ni [list all invert]} {return}

    switch -- $fn {
	all {
	    if {$cellmode} {
		$w cellselection set -nohidden -recurse \
		    [list [GetFirstItem $w] [FirstColumnId $w]] \
		    [list [GetLastItem $w] [LastColumnId $w]]
	    } else {
		$w selection set -nohidden -recurse [GetFirstItem $w] [GetLastItem $w]
	    }
	}
	column {
	    if {$cellmode} {
		$w cellselection set -nohidden -recurse \
		    [list [GetFirstItem $w] $column] [list [GetLastItem $w] $column]
	    }
	}
	invert {
	    if {$cellmode} {
		$w cellselection toggle -nohidden -recurse \
		    [list [GetFirstItem $w] [FirstColumnId $w]] \
		    [list [GetLastItem $w] [LastColumnId $w]]
	    } else {
		$w selection toggle -nohidden -recurse [GetFirstItem $w] [GetLastItem $w]
	    }
	}
	row {
	    if {$cellmode} {
		$w cellselection set -nohidden [list $item [FirstColumnId $w]] \
		    [list $item [LastColumnId $w]]
	    } else {
		$w selection set $item
	    }
	}
    }
}

#
# SelectNone -- Clear selection
#
proc ::ttk::treeview::SelectNone {w} {
    if {[$w instate disabled]} return

    set mode [$w cget -selectmode]
    if {$mode ne "browse"} {
	$w cellselection set {}
	$w selection set {}
    } else {
	set item [$w focus]
	set cell [$w cellfocus]
	$w cellselection set {}
	$w selection set {}
	if {$item ne ""} {
	    $w selection set [list $item]
	} elseif {$cell ne ""} {
	    $w cellselection set [list $cell]
	}
    }
}

#
# Motion -- pointer motion binding when no button pressed.
#	Sets cursor, active element ...
#
proc ::ttk::treeview::Motion {w x y} {
    variable State

    ttk::saveCursor $w State(userConfCursor) [ttk::cursor hresize]

    set cursor $State(userConfCursor)
    set activeHeading {}

    switch -- [$w identify region $x $y] {
	heading { set activeHeading [$w identify column $x $y] }
	separator { set cursor hresize }
    }

    ttk::setCursor $w $cursor
    ActivateHeading $w $activeHeading
}

#
# ActivateHeading -- track active heading element
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
	array set State [list activeHeading $heading activeWidget $w]
    }
}

#
# MouseSelect -- Select item or cell using button 1
#
proc ::ttk::treeview::MouseSelect {w x y op} {
    if {![winfo exists $w] || [$w instate disabled]} return

    if {[$w cget -selectmode] ni [list "single" "extended" "multiple"]} {
	return
    }

    if {[$w cget -selecttype] eq "cell"} {
	set item [$w identify item $x $y]
	set column [$w identify column $x $y]
	if {$column ne ""} {
	    set cell [list $item $column]
	} else {
	    set cell ""
	}
    } else {
	set item [$w identify item $x $y]
	set cell ""
    }

    if {$item ne "" } {
	SelectOp $w $item $cell $op
    }
}

#
# DoubleClick -- Double-Button-1 binding.
#
proc ::ttk::treeview::DoubleClick {w x y} {
    if {![winfo exists $w] || [$w instate disabled]} return

    if {[set item [$w identify item $x $y]] ne ""} {
	set element [$w identify element $x $y]
	if {$element eq "Treeitem.indicator"} {
	    ToggleOpenState $w $item
	} else {
	    set column [$w identify column $x $y]
	    ActivateItem $w $item $column
	}
    } else {
	Press $w $x $y;# perform single-click action
    }
}

#
# Interactive column resize, column move, and expand selection handlers
#
proc ::ttk::treeview::Press {w x y} {
    if {![winfo exists $w] || [$w instate disabled]} return

    focus $w
    switch -- [$w identify region $x $y] {
	nothing { }
	heading { Heading.press $w $x $y }
	separator { Resize.press $w $x $y }
	tree -
	cell { Select.press $w $x $y }
    }
}

proc ::ttk::treeview::Drag {w x y} {
    variable State
    if {![winfo exists $w] || [$w instate disabled]} return

    switch $State(pressMode) {
	heading	{ Heading.drag $w $x $y }
	resize	{ Resize.drag $w $x }
	selection { Select.drag $w $x $y }
    }
}

proc ::ttk::treeview::Release {w x y} {
    variable State
    if {![winfo exists $w] || [$w instate disabled]} return

    switch $State(pressMode) {
	heading	{ Heading.release $w $x $y }
	resize	{ Resize.release $w $x }
	selection { }
    }
    set State(pressMode) none
    Motion $w $x $y
}

#
# Interactive item/cell expand selection
#
proc ttk::treeview::Select.press {w x y} {
    variable State

    set item [$w identify item $x $y]
    if {$item eq ""} return

    if {[$w cget -selecttype] eq "cell"} {
	set cell [$w identify cell $x $y]
    } else {
	set cell ""
    }

    switch -glob -- [$w identify element $x $y] {
	*indicator -
	*disclosure { ToggleOpenState $w $item }
	default {
	    SelectOp $w $item $cell choose
	    if {[$w cget -selectmode] in [list browse extended]} {
		set State(pressMode) "selection"
	    }
	}
    }
    array set State [list current $item currentCell $cell]
}

proc ttk::treeview::Select.drag {w x y} {
    variable State
    if {$State(pressMode) ne "selection"} return
    lassign [$w current] item column

    # Autoscroll equivalent
    set hh [expr {"headings" in [$w cget -show] ? [ttk::style configure Treeview -rowheight] : 0}]
    set ht [winfo height $w]
    set wd [winfo width $w]
    if {$y >= $ht} {
	$w yview scroll 1 units
	set y [expr {$ht - 5}]
    } elseif {$y < $hh} {
	$w yview scroll -1 units
	set y [expr {$hh + 5}]
    } elseif {$x >= $wd} {
	$w xview scroll 5 units
	set x [expr {$wd - 5}]
    } elseif {$x <= 0} {
	$w xview scroll -5 units
	set x 5
    }

    if {$item eq "" || $column eq ""} {
	set item [$w identify item $x $y]
	set column [$w identify column $x $y]
    }
    if {$item eq ""} {
	return
    }

    # Adjust selection for cell or item mode
    if {[$w cget -selecttype] eq "cell"} {
	if {$column eq ""} return
	set cell [list $item $column]
	    set mode [$w cget -selectmode]
	    if {$mode eq "browse"} {
		$w cellselection set [list $cell]
		$w cellfocus $cell
	    } elseif {$mode eq "extended"} {
		$w cellselection set [$w cellselection anchor] $cell
	    }
	array set State [list current $item currentCell $cell]

    } else {
	if {$item ne $State(current)} {
	    set mode [$w cget -selectmode]
	    if {$mode eq "browse"} {
		$w selection set [list $item]
		$w focus $item
	    } elseif {$mode eq "extended"} {
		$w selection set [$w selection anchor] $item
	    }
	}
	array set State [list current $item currentCell {}]
    }
}

#
# Interactive column resizing
#
proc ::ttk::treeview::Resize.press {w x y} {
    variable State
    array set State [list pressMode "resize" resizeColumn [$w identify column $x $y]]
}

proc ::ttk::treeview::Resize.drag {w x} {
    variable State
    $w drag $State(resizeColumn) $x
}

proc ::ttk::treeview::Resize.release {w x} {
    $w drop
}

#
# Interactive column move
#
proc ::ttk::treeview::Heading.press {w x y} {
    variable State
    set column [$w column [$w identify column $x $y] -id]
    if {$column eq ""} return

    # Get column positions
    set columns [$w cget -displaycolumns]
    if {[lindex $columns 0] eq "#all"} {
	set columns [$w cget -columns]
    }
    set x0 0
    set x1 0
    set list [list]
    foreach col [concat [list #0] $columns] {
	lappend list $col [set x0 $x1] [incr x1 [$w column $col -width]]
    }

    array set State [list pressMode "heading" heading $column moved 0 \
	cursor [$w cget -cursor] activeHeading "" columns $list x0 $x]
    $w heading $column state pressed
}

proc ::ttk::treeview::Heading.drag {w x y} {
    variable State
    set halo 5

    if {$State(pressMode) ne "heading" || $State(heading) eq "#0" ||
	    abs($State(x0) - $x) < $halo} {
	return
    }

    if {[$w identify region $x $y] in [list "heading" "separator"]} {
	set column [$w column [$w identify column $x $y] -id]
	if {$column eq "#0"} return

	# Show move cursor
	if {[$w cget -cursor] eq $State(cursor)} {
	    set cursor [ttk::cursor move]
	    if {$cursor eq ""} {
	        set cursor "fleur"
	    }
	    set State(cursor) [$w cget -cursor]
	    ttk::setCursor $w $cursor
	}

	# Activate column to be moved
	set active $State(activeHeading)
	if {$active ne $column} {
	    if {$active ne ""} {
		$w heading $active state !active
	    }
	    set State(activeHeading) $column
	    $w heading $column state active
	}

	# Move column if crosses into another column
	set columns [$w cget -displaycolumns]
	if {[lindex $columns 0] eq "#all"} {
	    set columns [$w cget -columns]
	}
	set index -1
	foreach {col x0 x1} $State(columns) {
	    if {$x >= $x0 && $x <= $x1} {
		set idx [lsearch $columns $State(heading)]
		set columns [linsert [lremove $columns $idx] $index $State(heading)]
		$w configure -displaycolumns $columns
		set State(moved) 1
	    }
	    incr index
	}
    }
}

proc ::ttk::treeview::Heading.release {w x y} {
    variable State
    if {$State(pressMode) ne "heading"} return

    if {[$w identify region $x $y] eq "heading"} {
	set column [$w column [$w identify column $x $y] -id]
	if {!$State(moved)} {
	    # Do sort
	    after 0 [$w heading $State(heading) -command]
	}
	set State(moved) 0
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
# Selection modes
#

#
# SelectOp $w $item $cell [ moveto | choose | extend | toggle ] --
#	Dispatch to appropriate selection operation
#	depending on current value of -selectmode.
#
# Where:moveto = Keyboard traverse move to cell and select it
#	choose = Button select item or cell
#	toggle = Button or keyboard toggle open/close item and select it
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
# BrowseTo -- navigate to specified item; set focus and selection
#
proc ::ttk::treeview::BrowseTo {w item cell {op set}} {
    variable State

    if {$op ne "none"} {
	if {$cell ne ""} {
	    $w cellselection anchor $cell
	    $w cellselection $op [list $cell]
	    $w cellfocus $cell
	    $w see {*}$cell
	} else {
	    $w selection anchor $item
	    $w selection $op [list $item]
	    $w focus $item
	    $w see $item
	}
    } else {
	if {$cell ne ""} {
	    $w cellfocus $cell
	    $w see {*}$cell
	} else {
	    $w focus $item
	    $w see $item
	}
    }
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
	    $w see {*}$cell
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
# User interaction utilities.
#

#
# OpenItem, CloseItem -- Set the open state of an item, generate event
# Doesn't change selection state.
#
proc ::ttk::treeview::OpenItem {w item args} {
    if {[$w instate disabled]} return

    # If no item, use focus
    if {$item eq ""} {
	if {[$w cget -selecttype] eq "item"} {
	    set item [$w focus]
	} else {
	    set cell [$w cellfocus]
	    lassign $cell item column
	}
    }

    # Open item
    if {$item ne ""} {
	event generate $w <<TreeviewOpen>>
	$w expand {*}$args [list $item]
    }
}

proc ::ttk::treeview::CloseItem {w item} {
    if {[$w instate disabled]} return

    # If no item, use focus
    if {$item eq ""} {
	if {[$w cget -selecttype] eq "item"} {
	    set item [$w focus]
	} else {
	    set cell [$w cellfocus]
	    lassign $cell item column
	}
    }

    # Close item
    if {$item ne ""} {
	$w collapse [list $item]
	event generate $w <<TreeviewClose>>
    } else {
	return
    }

    # If focus item is not visible, move focus to item
    if {[$w cget -selecttype] eq "item"} {
	set focus [$w focus]
	set column ""
    } else {
	lassign [$w cellfocus] focus column
    }
    if {$focus ne "" && ![$w visible $focus]} {
	if {[$w cget -selecttype] eq "item"} {
	    SelectOp $w $item {} moveto
	} else {
	    SelectOp $w $item [list $item #0] moveto
	}
    }
}

#
# ToggleOpenState -- toggle opened/closed state of item
#
proc ::ttk::treeview::ToggleOpenState {w item} {
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
# ToggleSelected -- toggle selected state of item
#
proc ::ttk::treeview::ToggleSelected {w op} {
    if {[$w instate disabled]} return

    if {[$w cget -selectmode] in [list "none" "browse"]} {
	return
    }

    if {[$w cget -selecttype] eq "cell"} {
	set cell [$w cellfocus]
	lassign $cell item column
    } else {
	set item [$w focus]
	set cell ""
    }

    if {$item ne ""} {
	SelectOp $w $item $cell toggle
    }
}

#
# ActivateItem -- Default action for invoke
#
# Order:
# 1. If ::ttk::treeview::EditItem exists, call it.
# 2. If in cell mode, move down 1 cell.
# 3. If item has children, open it.
# 4. If not, select it.
#
proc ::ttk::treeview::ActivateItem {w {item {}} {column {}}} {
    if {[$w instate disabled]} return

    set cellmode [expr {[$w cget -selecttype] eq "cell"}]
    set skip 0

    if {$item eq ""} {
	if {$cellmode} {
	    set cell [$w cellfocus]
	    lassign $cell item column
	} else {
	    set item [$w focus]
	    set cell ""
	}
    } else {
	set skip 1
	set cell [list $item $column]
    }

    if {[info procs EditItem] ne ""} {
	EditItem $w $item $column
    } elseif {$cellmode && !$skip} {
	KeyNav $w down
    } elseif {[$w haschildren $item]} {
	ToggleOpenState $w $item
	if {$cellmode} {
	    $w cellfocus $cell
	} else {
	    $w focus $item
	}
    } else {
	SelectOp $w $item $cell choose
    }
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
# Copy to clipboard independent of -exportselection and PRIMARY selection
#
proc ::ttk::treeview::CopyToClipboard {w} {
    set data ""
    set format [expr {$::tcl_platform(platform) ne "windows" ? "UTF8_STRING" : "STRING"}]

    if {[$w instate disabled]} return

    # Determine which columns are shown
    set columns [$w cget -displaycolumns]
    if {[lindex $columns 0] eq "#all"} {
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

	# Get selected cells by item and in display column order. The cells need
	# to be reordered since they are stored in selected order, not column order.
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
