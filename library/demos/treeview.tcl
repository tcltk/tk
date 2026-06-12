# treeview.tcl --
#
# This demonstration script creates a toplevel window containing a Ttk
# treeview widget.

if {![info exists widgetDemo]} {
    error "This script should be run from the \"widget\" demo."
}

package require Tk 9.0-


####################

#
# Create menubar
#
proc create_menubar {w tv} {
    set m [menu $w]

    # Add items
    foreach name [list file edit settings item] {
	set title [string totitle $name]
	set $name [menu [format "%s.%s" $m $name] -title $title]
	$m insert end cascade $name -label $title -menu [set $name]
    }

    # File menu
    foreach {name command} [list Close [list destroy [winfo toplevel $tv]]] {
	$file insert end command $name -label $name -command $command
    }

    # Edit menu
    foreach {name command} [list \
	Copy [list generate_event $tv <<Copy>>] \
	- - \
	"Select All" [list generate_event $tv <<SelectAll>>] \
	"Clear Selection" [list generate_event $tv <<SelectNone>>] \
	"Invert Selection" [list generate_event $tv <<SelectInvert>>]] {
	if {$name ne "-"} {
	    $edit insert end command $name -label $name -command $command
	} else {
	    $edit insert end separator
	}
    }

    # Columns menu
    set columns [menu $settings.columns]
    foreach name [list "auto size all" "disable stretch" "stretch all"] {
	set title [string totitle $name]
	$columns insert end command $name -label $title \
	    -command [list column_handler $tv $name]
    }

    # Focus menu
    set focus [menu $settings.focus]
    foreach name [list clear] {
	set title [string totitle $name]
	$focus insert end command $name -label $title \
	    -command "$tv focus {};$tv cellfocus {}"
    }

    # Show menu
    set show [menu $settings.show]
    foreach name [list headings tree] {
	set title [string totitle $name]
	set var ::treeview($name)
	set $var [expr {$name in [$tv cget -show]}]
	$show insert end checkbutton $name -label $title \
	    -variable $var -command [list show_handler $tv $name $var]
    }

    # State menu
    set state [menu $settings.state]
    set var ::treeview(state)
    set $var "normal"
    foreach name [list normal readonly disabled] {
	set title [string totitle $name]
	$state insert end radiobutton $name -label $title -value $name \
	    -variable $var -command [list state_handler $tv $var]
    }

    # Themes menu
    set theme [menu $settings.theme]
    set var ::treeview(theme)
    set $var [ttk::style theme use]
    foreach name [lsort [ttk::style theme names]] {
	$theme insert end radiobutton $name -label [string totitle $name] \
	    -value $name -variable $var -command [list theme_handler $w $name]
    }

    # Styles menu
    set style [menu $settings.style]
    set var ::treeview(style)
    set $var [$tv style]
    foreach name [list Treeview CheckTreeview] {
	$style insert end radiobutton $name -label $name -value $name \
	    -variable $var -command [list $tv configure -style $name]
    }

    # Zoom menu
    set zoom [menu $settings.zoom]
    set var ::treeview(zoom)
    set $var 100
    foreach val [list 50 75 90 100 110 125 150 175 200 225 250] {
	$zoom insert end radiobutton val$val -label [format "%d%%" $val] \
	    -value $val -variable $var -command [list zoom_handler $w $val]
    }

    # Settings menu
    foreach {name type} [list columns cascade focus cascade show cascade state \
	    cascade style cascade theme cascade zoom cascade striped checkbutton] {
	set title [string totitle $name]
	if {$type eq "cascade"} {
	    $settings insert end $type $name -label $title -menu [set $name]
	} else {
	    set ::treeview($name) [$tv cget -$name]
	    set var ::treeview($name)
	    $settings insert end $type $name -label $title -variable $var \
		-command [subst -nocommands {$tv configure -$name [set $var]}]
	}
    }

    # Item menu
    foreach {title} [list \
	    "Collapse All" "Expand All" "Hide All" "Unhide All" - \
	    "Collapse Items" "Expand Items" "Hide Items" "Unhide Items"] {
	if {$title eq "-"} {
	    $item insert end separator
	} else {
	    set fn [string tolower [lindex [split $title] 0]]
	    set all [string match "*All" $title]
	    set name [string map [list " " ""] [string tolower $title]]
	    $item insert end command $name -label $title -command [list fn_callback $tv $fn $all]
	}
    }
    return $m
}

#
# Item callback
#
proc fn_callback {w fn all} {
    if {$all} {
	$w $fn -recurse {}
    } else {
	$w $fn [$w selection]
    }
    event generate $w "<<TreeviewSelect>>"
}

#
# Events
#
proc generate_event {w event} {
    after idle [list focus $w]
    after idle [list event generate $w $event]
    log_msg "Generate $event event"
}

#
# Column handler
#
proc column_handler {w fn} {
    if {$fn eq "auto size all"} {
	::ttk::treeview::AutoSizeAllColumns $w
	return
    }

    set bool [expr {$fn eq "stretch all"}]
    set columns [$w cget -displaycolumns]
    if {[lindex $columns 0] eq "#all"} {
	set columns [$w cget -columns]
    }
    if {"tree" in [$w cget -show]} {
	set columns [concat [list #0] $columns]
    }
    foreach column $columns {
	$w column $column -stretch $bool
    }
}

#
# Show handler
#
proc show_handler {w name var} {
    set show [$w cget -show]

    if {[set $var]} {
	if {$name ni $show} {
	    lappend show $name
	}
    } else {
	if {$name in $show} {
	    set index [lsearch $show $name]
	    set show [lreplace $show $index $index]
	}
    }
    $w configure -show $show
}

#
# State handler
#
proc state_handler {w var} {
    set state [set $var]
    if {$state eq "normal"} {
	$w state [list !disabled !readonly]
    } elseif {$state eq "readonly"} {
	$w state [list readonly !disabled]
    } else {
	$w state [list !readonly disabled]
    }
}

#
# Theme change handler
#
proc theme_handler {w {name {}}} {
    if {$name eq ""} {
	set theme [ttk::style theme use]
	set ::treeview(theme) $theme
    } else {
	ttk::style theme use $name
    }
    log_msg "Switched to $name theme"
}

#
# Zoom init
#
proc zoom_init {w} {
    if {[info vars ::scaling] eq ""} {
	set ::scaling [tk scaling -displayof $w]
	set ::treeview(zoom) 100

	foreach font [font names] {
	    set ::fontSize($font) [font actual $font -size]
	}
    } else {
	set ::treeview(zoom) [expr {round([tk scaling] * 100.0 / $::scaling)}]
    }
}

#
# Zoom handler
#
proc zoom_handler {w percent} {
    set scale [expr {($percent / 100.0) * $::scaling}]
    tk scaling -displayof $w $scale
    log_msg [format "Zoom %d%%" $percent]

    # Scale font
    set winSys [tk windowingsystem]
    if {($winSys eq "aqua") || ($winSys eq "x11" &&
	    ![catch {tk::pkgconfig get fontsystem} fs] && $fs eq "xft")} {
	# Adapt the font sizes to the new scaling factor
	foreach font [array names ::fontSize] {
	    set size [expr {round($::fontSize($font) * $percent / 100.0)}]
	    font configure $font -size $size
	}
    } else {
	# Reapply font attributes to force change
	foreach font [font names] {
	    font configure $font {*}[font actual $font]
	}
    }

    # Refresh the contents of the ttk::entry and ttk::combobox widgets
    # in this toplevel, using the level-order traversal algorithm
    set lst1 [winfo children .treeview]
    while {[llength $lst1] != 0} {
	set lst2 {}
	foreach w $lst1 {
	    if {[winfo class $w] in [list TEntry TCombobox TSpinbox]} {
		$w configure -font [$w cget -font]
	    }

	    foreach child [winfo children $w] {
		lappend lst2 $child
	    }
	}
	set lst1 $lst2
    }

    # Scale the SVG images created by Tk (including the file icons and
    # the images used for the elements of the ttk::toggleswitch widget)
    foreach img [image names] {
	if {[image type $img] eq "photo"} {
	    set fmt [$img cget -format]
	    if {[string match "svg -scale *" $fmt]} {
		$img configure -format $::tk::svgFmt
	    } elseif {[string match "svg -scaletoheight*" $fmt]} {
		set iconSize [expr {16 * $::tk::scalingPct / 100}]
		$img configure -format [list svg -scaletoheight $iconSize]
	    }
	}
    }
    interp alias {} ::ttk::toggleswitch::CreateImg \
		 {} image create photo -format $::tk::svgFmt
}

####################

#
# Create treeview widget
#
proc create {w} {
    # Container frame
    set f [ttk::frame $w]

    # Treeview and scrollbars
    set tv [ttk::treeview $f.tv -striped 1 -xscrollcommand [list $f.sx set] \
	-yscrollcommand [list $f.sy set]]
    ttk::scrollbar $f.sx -orient horizontal -command [list $tv xview]
    ttk::scrollbar $f.sy -orient vertical -command [list $tv yview]
    grid $tv $f.sy -sticky nsew
    grid $f.sx -sticky nsew
    grid rowconfigure $f 0 -weight 1
    grid columnconfigure $f 0 -minsize 300 -weight 1
    return $tv
}

####################

#
# Selection control options toolbar
#
proc selection_toolbar {w tv} {
    # Container frame
    set f [ttk::frame $w -padding {5 3 5 3}]

    # Label
    set l [ttk::label $f.l -text "Selection Mode:"]
    grid $l -row 0 -column 0

    # Selection mode
    set modes [list none browse single multiple extended]
    set ::sel_mode [$tv cget -selectmode]
    set cb [ttk::combobox $f.modes -values $modes -textvariable ::sel_mode \
	-width 10 -state readonl -exportselection 0]
    trace add variable ::sel_mode write [list var_callback $tv -selectmode]
    grid $cb -row 0 -column 1

    # Label
    set l [ttk::label $f.l2 -text "Type:"]
    grid $l -row 0 -column 2 -padx {10 0}

    # Selection type
    set types [list cell item]
    set ::sel_type [$tv cget -selecttype]
    set cb [ttk::combobox $f.types -values $types -textvariable ::sel_type \
	-width 5 -state readonly -exportselection 0]
    trace add variable ::sel_type write [list var_callback $tv -selecttype]
    grid $cb -row 0 -column 3

    # Clear selection
    set b [ttk::button $f.clear -text "Clear" \
	-command [list generate_event $tv <<SelectNone>>]]
    grid $b -row 0 -column 4 -pady 0 -ipady 0 -padx 20

    # Get selection
    set b [ttk::button $f.get -text "Get" -command [list get_selection $tv $f.sel]]
    set e [ttk::entry $f.sel]
    grid $b -row 0 -column 5
    grid $e -row 0 -column 6 -sticky ew -ipady 2
    grid columnconfigure $f 6 -weight 1
    return $f
}

#
# Update widget option when value changes callback
#
proc var_callback {w opt var index op} {
     if {[set val [set $var]] ne ""} {
	$w configure $opt $val
    }
}

#
# Callback to get selected items or cells
#
proc get_selection {w e} {
    if {[$w cget -selecttype] eq "cell"} {
	set cmd [list $w cellselection]
    } else {
	set cmd [list $w selection]
    }
    $e delete 0 end
    $e insert 0 [eval $cmd]
    log_msg $cmd
}

####################

#
# Search toolbar
#
proc search_toolbar {w tv} {
    # Container frame
    set f [ttk::frame $w -padding {5 3 5 3}]

    # Label
    set l [ttk::label $f.l -text "Search Mode:"]
    grid $l -row 0 -column 0

    # Search mode
    set modes [list exact glob regexp]
    set ::search(-mode) "exact"
    set cb [ttk::combobox $f.modes -values $modes -textvariable ::search(-mode) \
	-width 6 -state readonly -exportselection  0]
    grid $cb -row 0 -column 1

    # Label
    set l [ttk::label $f.l2 -text "Type:"]
    grid $l -row 0 -column 2 -padx {10 0}

    # Search type
    set types [list ascii dictionary integer real]
    set ::search(-type) "ascii"
    set cb [ttk::combobox $f.types -values $types -textvariable ::search(-type) \
	-width 9 -state readonly -exportselection  0]
    grid $cb -row 0 -column 3

    # Label
    set l [ttk::label $f.l3 -text "Column:"]
    grid $l -row 0 -column 4 -padx {10 0}

    # Search in column
    set ::search(-column) ""
    set cb [ttk::combobox $f.cols -values [concat [list "" #0] [$tv cget -columns]] \
	-textvariable ::search(-column) -width 10 -state readonly -exportselection  0]
    grid $cb -row 0 -column 5

    # Search all
    set ::search(-all) 0
    set cb [ttk::checkbutton  $f.all -text "All" -variable ::search(-all)]
    grid $cb -row 0 -column 6 -padx {10 0}

    # Search in hidden
    set ::search(-hidden) 0
    set cb [ttk::checkbutton  $f.hidden -text "Hidden" -variable ::search(-hidden)]
    grid $cb -row 0 -column 7

    # Search case insensitive
    set ::search(-nocase) 0
    set cb [ttk::checkbutton  $f.nocase -text "Ignore Case" -variable ::search(-nocase)]
    grid $cb -row 0 -column 8

    # Search not
    set ::search(-not) 0
    set cb [ttk::checkbutton  $f.not -text "Not" -variable ::search(-not)]
    grid $cb -row 0 -column 9

    # Search recurse
    set ::search(-recurse) 1
    set cb [ttk::checkbutton  $f.recurse -text "Recurse" -variable ::search(-recurse)]
    grid $cb -row 0 -column 10

    # Search wrap-around
    set ::search(-wraparound) 0
    set cb [ttk::checkbutton  $f.wrap -text "Wraparound" -variable ::search(-wraparound)]
    grid $cb -row 0 -column 11

    # Label
    set l [ttk::label $f.l4 -text "Pattern:"]
    grid $l -row 0 -column 12 -padx {10 0}

    # Get Pattern
    set ::search(pattern) ""
    set e [ttk::entry $f.pat -textvariable ::search(pattern) -width 20]
    grid $e -row 0 -column 13 -sticky ew -ipady 2
    grid columnconfigure $f 13 -weight 1

    # Find previous
    set b [ttk::button $f.prev -text "Previous" -width 8 -command [list search_callback $tv -backwards]]
    grid $b -row 0 -column 14

    # Find next
    set b [ttk::button $f.next -text "Next" -width 4 -command [list search_callback $tv -forwards]]
    grid $b -row 0 -column 15
    return $f
}

#
# Get previous column
#
proc prev_cell {w} {
    set focus [$w cellfocus]
    if {$focus eq ""} return
    lassign $focus item column

    set columns [$w cget -displaycolumns]
    if {[lindex $columns 0] eq "#all"} {
	set columns [$w cget -columns]
    }
    if {"tree" in [$w cget -show]} {
	set columns [concat [list #0] $columns]
    }

    set index [lsearch $columns $column]
    set column [lindex $columns [incr index -1]]
    if {$column eq ""} {
	set item [$w before $item]
	set column [lindex $columns end]
    }
    return [list $item $column]
}

#
# Get next column
#
proc next_cell {w} {
    set focus [$w cellfocus]
    if {$focus eq ""} return
    lassign $focus item column

    set columns [$w cget -displaycolumns]
    if {[lindex $columns 0] eq "#all"} {
	set columns [$w cget -columns]
    }
    if {"tree" in [$w cget -show]} {
	set columns [concat [list #0] $columns]
    }

    set index [lsearch $columns $column]
    set column [lindex $columns [incr index]]
    if {$column eq ""} {
	set item [$w after $item]
	set column [lindex $columns 0]
    }
    return [list $item $column]
}

#
# Do search callback
#
proc search_callback {w direction} {
    set cmd [list $w search {} $direction]

    if {[$w cget -selecttype] eq "item"} {
	set focus [$w focus]
	if {$focus ne ""} {
	    if {$direction eq "-forwards"} {
		set start [$w after $focus]
	    } else {
		set start [$w before $focus]
	    }
	    if {$start ne ""} {
		lappend cmd -start $start
	    }
	}
    } else {
	set focus [$w cellfocus]
	lappend cmd -cell
	if {$focus ne ""} {
	    set start ""
	    if {$::search(-column) ne ""} {
		if {$direction eq "-forwards"} {
		    set item [$w after $focus]
		} else {
		    set item [$w before $focus]
		}
		if {$item ne ""} {
		    set start [list $item $::search(-column)]
		}
	    } else {
		if {$direction eq "-forwards"} {
		    set start [next_cell $w]
		} else {
		    set start [prev_cell $w]
		}
	    }
	    if {[llength $start] > 0} {
		lappend cmd -start $start
	    }
	}
    }

    foreach opt [list -mode -type] {
	lappend cmd -$::search($opt)
    }

    foreach opt [list -column] {
	if {$::search($opt) ne ""} {
	    lappend cmd $opt $::search($opt)
	}
    }

    foreach opt [list -all -hidden -nocase -not -recurse -wraparound] {
	if {$::search($opt)} {
	    lappend cmd $opt
	}
    }

    # Pattern
    if {$::search(pattern) ne ""} {
	lappend cmd $::search(pattern)
    } else {
	return
    }

    # Do search
    set sel [eval $cmd]
    if {[llength $sel] > 0} {
	if {[$w cget -selecttype] eq "item"} {
	    $w selection set $sel
	    if {[llength $sel] > 0} {
		$w focus [lindex $sel end]
		$w see [$w focus]
	    }
	} else {
	    $w cellselection set $sel
	    if {[llength $sel] > 0} {
		$w cellfocus [lindex $sel end]
		$w see {*}[$w cellfocus]
	    }
	}
	log_msg [format "Found \"%s\" for search \"%s\"" $sel $cmd]
    } else {
	log_msg [format "No matches found for \"%s\"" $cmd]
    }
}

####################

#
# Create status toolbar
#
proc statusbar {w tv} {
    set col -1

    # Container frame
    set f [ttk::frame $w -padding {5 3 5 3}]

    # Focus
    set l [ttk::label $f.lf -text "Focus:"]
    grid $l -row 0 -column [incr col] -sticky e
    set focus [ttk::entry $f.focus -width 12]
    grid $focus -row 0 -column [incr col] -padx {0 2} -sticky w -ipady 2

    # Depth
    set l [ttk::label $f.lp -text "Depth:"]
    grid $l -row 0 -column [incr col] -padx {2 0} -sticky e
    set depth [ttk::entry $f.depth -width 2]
    grid $depth -row 0 -column [incr col] -padx {0 10} -sticky w -ipady 2

    # descendants
    set l [ttk::label $f.ld -text "Descendants:"]
    grid $l -row 0 -column [incr col] -padx {2 0} -sticky e
    set descendants [ttk::entry $f.descendants -width 2]
    grid $descendants -row 0 -column [incr col] -padx {0 10} -sticky w -ipady 2

    # Message
    set msg [ttk::entry $f.msg -textvariable ::msg]
    grid $msg -row 0 -column [incr col] -padx {2 2} -sticky ew -ipady 2
    grid columnconfigure $f $col -weight 1

    # Selection
    set l [ttk::label $f.ls -text "Items:"]
    grid $l -row 0 -column [incr col] -padx {10 0} -sticky e
    set items [ttk::entry $f.sel -width 20]
    grid $items -row 0 -column [incr col] -sticky w -ipady 2

    bind $tv "<<TreeviewFocus>>" [list focus_callback $tv $focus $depth $descendants]
    bind $tv "<<TreeviewSelect>>" [list selection_callback $tv $items]
    after 10 [list selection_callback $tv $items]
    return $f
}

#
# Update Focus status callback
#
proc focus_callback {w focus depth descendants} {
    set mode [$w cget -selecttype]
    set item [$w focus]
    set cell [$w cellfocus]
    if {$item eq "" && $cell ne ""} {
	lassign $cell item column
    }

    # Update focus item
    $focus configure -state normal
    $focus delete 0 end
    $focus insert 0 [expr {$mode eq "cell" ? $cell : $item}]
    $focus configure -state readonly

    # Update depth
    $depth configure -state normal
    $depth delete 0 end
    $depth insert 0 [$w depth $item]
    $depth configure -state readonly

    # Update descendants
    $descendants configure -state normal
    $descendants delete 0 end
    $descendants insert 0 [$w size -recurse $item]
    $descendants configure -state readonly
}

#
# Update selection status callback
#
proc selection_callback {w e} {
    set cell [$w cellfocus]

    # Get count of selected items
    set total [$w size -recurse {}]
    if {[$w cget -selecttype] eq "cell"} {
	set sel [llength [$w cellselection]]
	if {$sel > 0} {
	    set msg [format "%d cells selected" $sel $total]
	} else {
	    set msg [format "%d items" $total]
	}
    } else {
	set sel [$w selection size]
	if {$sel > 0} {
	    set msg [format "%d of %d items selected" $sel $total]
	} else {
	    set msg [format "%d items" $total]
	}
    }

    # Update selected items
    $e configure -state normal
    $e delete 0 end
    $e insert 0 $msg
    $e configure -state readonly
}

####################

#
# Add data to widget
#
proc populate {w} {
    set num 0
    set data [list \
	"Africa" [list \
	    [list Algeria Algiers DZD Arabic 47022473 2381740 29.1 {}] \
	    [list Egypt Cairo EGP Arabic 111247248 1001450 24.4 {}] \
	    [list "South Africa" Pretoria ZAR Afrikaans 60442647 1219090 30.4 {}]] \
	"Asia" [list \
	    [list China Beijing CNY Chinese 1416043270 9596960 40.2 {}] \
	    [list India "New Delhi" INR Hindi 1409128296 3287263 29.8 {}] \
	    [list Japan Tokyo JPY Japanese 122910438 377915 49.9 {}] \
	    [list Pakistan Islamabad PKR Punjabi 252363571 796095 22.9 {}] \
	    [list Russia Moscow RUB Russian 140820810 17098242 41.9 {}] \
	    [list "Saudi Arabia" Riyadh SAR Arabic 36544431 2149690 32.4 {}] \
	    [list Turkey Ankara TRY Turkish 84119531 783562 34.0 {}]] \
	"Antarctica" [list] \
	"Oceania" [list \
	    [list Australia Canberra AUD English 26768598 7741220 38.1 {}] \
	    [list "New Zealand" Wellington NZD English 5161211 268838 37.9 {}]] \
	"Europe" [list \
	    [list Austria Vienna EUR German 8967982 83871 44.9 {}] \
	    [list France Paris EUR French 68374591 643801 42.6 {}] \
	    [list Germany Berlin EUR German 84119100 357022 46.8 {}] \
	    [list Ireland Dublin EUR English 5233461 70273 40.2 {}] \
	    [list Italy Rome EUR Italian 60964931 301340 48.4 {}] \
	    [list Netherlands Amsterdam EUR Dutch 17772378 41543 42.2 {}] \
	    [list Portugal Lisbon EUR Portuguese 10207177 92090 46.4 {}] \
	    [list Spain Madrid EUR Spanish 47280433 505370 46.8 {}] \
	    [list Sweden Stockholm SEK Swedish 10589835 450295 41.1 {}] \
	    [list "United Kingdom" London GBP English 68459055 243610 40.8 {}]] \
	"North America" [list \
	    [list Canada Ottawa CAD English 38794813 9984670 42.6 {}] \
	    [list Mexico "Mexico City" MXN Spanish 130739927 1964375 30.8 {}] \
	    [list "United States" "Washington D.C." USD English 341963408 9833517 38.9 [list \
		[list California Sacramento 39538223] \
		[list Florida Tallahassee  21538187] \
		[list "New York" Albany 20201249] \
		[list Texas Austin 29145505]]]] \
	"South America" [list \
	    [list Argentina "Buenos Aires" ARS Spanish 46994384 2780400 33.3 {}] \
	    [list Brazil Brazilia BRL Portuguese 220051512 8515770 35.1 {}] \
	    [list Chile Santiago CLP Spanish 18664652 756102 36.9 {}] \
	    [list Colombia Bogotá COP Spanish 49588357 1138910 32.7 {}] \
	    [list Venezuela Caracas VED Spanish 31250306 912050 31.0 {}]]]

    # Configure headers
    set columns [list Capital Currency Language Population Size MedianAge]
    $w configure -columns $columns
    foreach col [concat [list #0] $columns] \
	    {text width type} [list Country 125 ascii Capital 110 ascii \
	    Currency 100 ascii Language 100 ascii Population 100 integer \
	    "Size (sq km)" 100 integer "Median Age" 100 real] {
	$w heading $col -anchor w -text $text \
	    -command [list SortByColumn $w $col $type] \
	    state [list !selected !alternate !user1]
	set anchor [expr {$type ne "ascii" ? "e" : "w"}]
	$w column $col -anchor $anchor -minwidth [expr {$width - 25}] -width $width
	if {$col eq "#0"} {
	    $w column $col -separator 1
	}
    }

    # Add data
    foreach {continent countries} $data {
	set parent [$w insert {} end -text $continent]
	foreach country $countries {
	    foreach {name capital currency language population size age states} $country {
		set item [$w insert $parent end -text $name -values [list $capital $currency $language $population $size $age]]
		foreach state $states {
		    foreach {name capital population} $state {
			$w insert $item end -text $name -values [list $capital $currency $language $population]
		    }
		}
	    }
	}
    }
    $w expand -recurse {}
}

#
# Set column header sort direction arrow.
#
# States: selected is increasing, alternate is decreasing, and user1 means use
# Aqua theme built-in sort arrows. We track last used sort direction via user6.
#
proc SortDirection {w columnId order} {
    foreach column [concat [list #0] [$w cget -columns]] {
	# Check if new sort column
	if {$column eq $columnId} {
	    if {$order eq "increasing"} {
		set states [list !selected alternate user1 user6]
	    } else {
		set states [list selected !alternate user1 !user6]
	    }
	    $w heading $column state $states
	} else {
	    $w heading $column state [list !selected !alternate !user1]
	}
    }
}

#
# Sort widget by column, order, and type.
#
proc SortByColumn {w column type} {
    ### set columns [$w cget -columns]
    if {$column eq ""} return
    set column [$w column $column -id]

    # Get new sort order (opposite of current order)
    set states [$w heading $column state]
    if {"alternate" in $states || "user6" in $states} {
	set order "decreasing"
    } elseif {"selected" in $states || "user6" ni $states} {
	set order "increasing"
    } else {
	set order "increasing"
    }

    # Sort items
    log_msg [list $w sort {} -column $column -$order -$type -nocase -ignoreempty -recurse]
    $w sort {} -column $column -$order -$type -nocase -ignoreempty -recurse

    # Show sort direction arrow in sort column, clear others
    SortDirection $w $column $order
}

####################

#
# Example handler to edit Treeview cell using ttk::entry widget
#
proc ::ttk::treeview::ActivateItem  {w item column} {
    if {$item eq "" || $column eq ""} return

    set anchor [expr {[$w column $column -anchor]}]
    switch -glob $anchor {
	"*e" {set justify "right"}
	"*w" {set justify "left"}
	default {set justify "center"}
    }

    # Create edit widget
    set e $w.e
    catch {destroy $e}
    set e [ttk::entry $e -justify $justify]
    $e insert 0 [$w set $item $column]
    foreach {binding fn} [list <Return> down <Shift-Return> up <Tab> right \
	<Shift-Tab> left <Escape> abort <FocusOut> none] {
	bind $e $binding [list ::ttk::treeview::EditDone $e $w $item $column $fn]
    }

    if {$column eq "#0"} {
	set i [ttk::style configure Item -indicatorsize]
	if {$i eq "" || $i == 0} {set i 12}
	set indent [winfo pixels $w $i]
	incr indent 2
	if {[$w cget -style] eq "CheckTreeview"} {
	    set indent [expr {$indent * 2}]
	}
    } else {
	set indent 0
    }

    # Place widget and adjust for tree indent
    if {[::ttk::style theme use] ne "aqua"} {
	set d 1
    } else {
	set d 4
    }
    lassign [$w bbox $item $column] x y wd ht
    incr x $indent
    incr y [expr {$d * -1}]
    set wd [expr {$wd - $indent}]
    incr ht [expr {$d * 2}]
    place $e -in $w -x $x -y $y -height $ht -width $wd
    focus $e

    # Set insertion cursor location
    set ix [expr {[winfo pointerx $w] - [winfo rootx $e] - $x + 3}]
    after 10 [list $e icursor @$ix]
}

proc ::ttk::treeview::EditDone {e w item column fn} {
    if {![winfo exists $e]} return

    # Copy edited value back into cell
    if {$fn ne "abort"} {
	$w set $item $column [$e get]
    }
    destroy $e
    focus $w

    # Move to new cell
    if {$fn ni [list "abort" "none"]} {
	::ttk::treeview::KeyNav $w $fn
    }
    return -code break
}

####################

#
# Log message
#
proc log_msg {msg} {
    set ::msg $msg
}

#
# Main routine
#
proc main {} {
    set w .treeview
    catch {destroy $w}
    toplevel $w
    wm title $w "Enhanced Treeview"
    wm iconname $w "treeview"
    positionWindow $w

    # Create treeview widget
    set tv [create $w.tf]
    grid $w.tf -sticky nsew -row 2
    grid rowconfigure $w 2 -weight 1
    grid columnconfigure $w 0 -weight 1
    wm minsize $w 300 300

    # Create theme change handler
    set tags [bindtags .]
    if {"MyMainWin" ni $tags} {
	bindtags . [linsert $tags 1 MyMainWin]
    }

    foreach event {<<ThemeChanged>> <<LightAqua>> <<DarkAqua>>} {
	bind $w $event [list theme_handler $w]
    }

    # Add data
    populate $tv

    # Add menu
    set m [create_menubar $w.m $tv]
    $w configure -menu $m

    # Selection Toolbar
    set tb [selection_toolbar $w.sel $tv]
    grid $tb -sticky nsew -row 0

    # Search Toolbar
    set tb [search_toolbar $w.search $tv]
    grid $tb -sticky nsew -row 1

    # Info message
    set l [ttk::label $w.info -text "To resize a column, drag the column\
	divider. To move a column, drag the column heading over another\
	heading or divider then drop it. To edit a cell, double-click on it.\
	Searches start from focus unless none, then first item."]
    grid $l -sticky nsew -row 3

    # Statusbar
    set tb [statusbar $w.stat $tv]
    grid $tb -sticky nsew -row 4

    ## See Code / Dismiss buttons
    grid [addSeeDismiss $w.buttons $w] - - -sticky ew -row 10
    grid columnconfigure $w 0 -weight 1

    # Init zoom
    zoom_init $w
    after idle [list focus $tv]
}

main
