# mclist.tcl --
#
# This demonstration script creates a toplevel window containing a Ttk
# tree widget configured as a multi-column listbox.

if {![info exists widgetDemo]} {
    error "This script should be run from the \"widget\" demo."
}

package require tk

set w .mclist
catch {destroy $w}
toplevel $w -class MCList
wm title $w "Multi-Column List"
wm iconname $w "mclist"
positionWindow $w

## Explanatory text
ttk::label $w.msg -font $font -wraplength 4i -justify left -anchor n -padding {10 2 10 6} -text "Ttk is the new Tk themed widget set. One of the widgets it includes is a tree widget, which can be configured to display multiple columns of informational data without displaying the tree itself. This is a simple way to build a listbox that has multiple columns. Clicking on the heading for a column will sort the data by that column. You can also change the width of the columns by dragging the boundary between them."
pack $w.msg -fill x

## See Code / Dismiss
pack [addSeeDismiss $w.seeDismiss $w {} {
    ttk::checkbutton $w.seeDismiss.cb1 -text Grid -variable mclistGrid -command tglGrid
}] -side bottom -fill x


ttk::frame $w.container
ttk::treeview $w.tree -columns {country capital currency} -show headings \
    -yscroll "$w.vsb set" -xscroll "$w.hsb set"
ttk::scrollbar $w.vsb -orient vertical -command "$w.tree yview"
ttk::scrollbar $w.hsb -orient horizontal -command "$w.tree xview"
pack $w.container -fill both -expand 1
grid $w.tree $w.vsb -in $w.container -sticky nsew
grid $w.hsb         -in $w.container -sticky nsew
grid column $w.container 0 -weight 1
grid row    $w.container 0 -weight 1

set upArrowData {
    <?xml version="1.0" encoding="UTF-8"?>
    <svg width="16" height="4" version="1.1" xmlns="http://www.w3.org/2000/svg">
     <path d="m4 4 4-4 4 4z" fill="%s"/>
    </svg>
}

set downArrowData {
    <?xml version="1.0" encoding="UTF-8"?>
    <svg width="16" height="4" version="1.1" xmlns="http://www.w3.org/2000/svg">
     <path d="m4 0 4 4 4-4z" fill="%s"/>
    </svg>
}

proc createArrowImages {} {
    set fgColor [ttk::style lookup . -foreground {} black]
    lassign [winfo rgb . $fgColor] r g b
    set fgColor [format "#%02x%02x%02x" \
	    [expr {$r >> 8}] [expr {$g >> 8}] [expr {$b >> 8}]]

    foreach dir {up down} {
	upvar ${dir}ArrowData imgData
	set data [format $imgData $fgColor]
	image create photo ${dir}Arrow -format $::tk::svgFmt -data $data]
    }
}

createArrowImages
foreach event {<<ThemeChanged>> <<LightAppearance>> <<DarkAppearance>>} {
    bind MCList $event { createArrowImages }
}
unset event

image create photo noArrow -format $tk::svgFmt -data {
    <?xml version="1.0" encoding="UTF-8"?>
    <svg width="16" height="4" version="1.1" xmlns="http://www.w3.org/2000/svg">
    </svg>
}

## The data we're going to insert
set data {
    Argentina		{Buenos Aires}		ARS
    Australia		Canberra		AUD
    Brazil		Brazilia		BRL
    Canada		Ottawa			CAD
    China		Beijing			CNY
    France		Paris			EUR
    Germany		Berlin			EUR
    India		{New Delhi}		INR
    Italy		Rome			EUR
    Japan		Tokyo			JPY
    Mexico		{Mexico City}		MXN
    Russia		Moscow			RUB
    {South Africa}	Pretoria		ZAR
    {United Kingdom}	London			GBP
    {United States}	{Washington, D.C.}	USD
}

## Code to insert the data nicely
set font [ttk::style lookup Heading -font {} TkDefaultFont]
set arrowWidth [expr {2 * round(4 * $tk::scalingPct / 100.0) + 1}]
set morePx [expr {3 + $arrowWidth + 2}]
foreach col {country capital currency} name {Country Capital Currency} {
    $w.tree heading $col -text $name -anchor w \
	-command [list SortByColumn $w.tree $col]
    $w.tree column $col -width [expr {[font measure $font $name] + $morePx}]
}
set font [ttk::style lookup Treeview -font {} TkDefaultFont]
foreach {country capital currency} $data {
    $w.tree insert {} end -values [list $country $capital $currency]
    foreach col {country capital currency} {
	set len [font measure $font "[set $col]  "]
	if {[$w.tree column $col -width] < $len} {
	    $w.tree column $col -width $len
	}
    }
}

# Set column header sort direction arrow.
#
# States: selected is increasing, alternate is decreasing, and user1 means use
# Aqua theme built-in sort arrows. We track last used sort direction via user6.
proc SortDirection {tree columnId order} {
    foreach column [$tree cget -columns] {
	# Check if new sort column
	if {$column eq $columnId} {
	    if {$order eq "increasing"} {
		set states [list !selected alternate user1 user6]
	    } else {
		set states [list selected !alternate user1 !user6]
	    }
	    $tree heading $column state $states
	} else {
	    $tree heading $column state [list !selected !alternate !user1]
	}
    }
}

# Sort widget by column and order.
proc SortByColumn {tree column} {
    if {$column eq ""} return
    set column [$tree column $column -id]

    # Get new sort order (opposite of current order)
    set states [$tree heading $column state]
    if {"alternate" in $states || "user6" in $states} {
	set order "decreasing"
    } elseif {"selected" in $states || "user6" ni $states} {
	set order "increasing"
    } else {
	set order "increasing"
    }

    # Sort items
    $tree sort {} -column $column -$order

    # Show sort direction arrow in sort column, clear others
    SortDirection $tree $column $order
}

set mclistGrid 0
proc tglGrid {} {
    if {$::mclistGrid} {
	.mclist.tree configure -stripe 1
	foreach col [.mclist.tree cget -columns] {
	    .mclist.tree column $col -separator 1
	}
    } else {
	.mclist.tree configure -stripe 0
	foreach col [.mclist.tree cget -columns] {
	    .mclist.tree column $col -separator 0
	}
    }
}
