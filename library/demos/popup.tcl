# popup.tcl --
#
# This demonstration script creates a window in which a context menu can be
# popped up with tk_popup, with menus of different sizes.

if {![info exists widgetDemo]} {
    error "This script should be run from the \"widget\" demo."
}

package require tk

set w .popup
catch {destroy $w}
toplevel $w
wm title $w "Popup Menu Demonstration"
wm iconname $w "popup"
positionWindow $w

label $w.msg -font $font -wraplength 4i -justify left -text "This window\
	demonstrates context menus posted with tk_popup.  Click in the gray\
	area with one of the mouse buttons selected below to pop up a menu\
	at the pointer.  The menu is normally posted to the right of and\
	below the pointer.  Move this window near a corner of the screen and\
	try again: the menu is then posted to the left of or above the\
	pointer, so that it fits on the screen.  A menu larger than the\
	screen covers the pointer; a quick click does not invoke the entry\
	under the pointer, the menu stays posted until you select an entry."
pack $w.msg -side top

## See Code / Dismiss buttons
set btns [addSeeDismiss $w.buttons $w]
pack $btns -side bottom -fill x

set popupDemo(height) small
set popupDemo(width) small
array set popupDemo {button1 0 button2 0 button3 1}
set popupDemo(status) "Click in the gray area."

# Fractions of the screen size for the choices of the menu height and width.
array set popupDemoFraction {small 0.25 large 0.67 huge 1.5}

menu $w.menu -tearoff 0

# popupDemoBuild --
# Fills the menu with entries until it has the selected height, and makes
# the labels long enough for it to have the selected width.

proc popupDemoBuild {menu} {
    global popupDemo popupDemoFraction
    $menu delete 0 end

    set h [expr {int($popupDemoFraction($popupDemo(height))
		     * [winfo screenheight $menu])}]
    set i 0
    while {1} {
	$menu add command -label "Entry $i" \
		-command [list set popupDemo(status) "You selected entry $i."]
	incr i
	# The position of the last entry, not the requested height of the
	# menu: a menu higher than the screen is limited to the screen.
	if {[$menu yposition last] >= $h} {
	    break
	}
    }

    set w [expr {int($popupDemoFraction($popupDemo(width))
		     * [winfo screenwidth $menu])}]
    set extra [expr {$w - [winfo reqwidth $menu]}]
    if {$extra > 0} {
	set font [$menu cget -font]
	set word [font measure $font " wide"]
	set fill [string repeat " wide" [expr {($extra + $word - 1) / $word}]]
	for {set j 0} {$j < $i} {incr j} {
	    $menu entryconfigure $j -label "Entry $j$fill"
	}
	$menu yposition last
    }
}

# popupDemoPost --
# Pops up the menu at the root coordinates x, y and reports where it was
# posted.

proc popupDemoPost {menu x y} {
    global popupDemo
    popupDemoBuild $menu
    tk_popup $menu $x $y
    update idletasks
    set x1 [winfo rootx $menu]
    set y1 [winfo rooty $menu]
    set x2 [expr {$x1 + [winfo width $menu]}]
    set y2 [expr {$y1 + [winfo height $menu]}]
    if {$x > $x1 && $x < $x2 && $y > $y1 && $y < $y2} {
	set where "over the pointer"
    } else {
	set where "beside the pointer"
    }
    set popupDemo(status) "The menu ([winfo width $menu]x[winfo height $menu])\
	    is posted $where."
}

frame $w.area -width 300 -height 150 -bg gray75 -relief sunken -bd 2
label $w.area.l -text "Click here" -bg gray75
place $w.area.l -relx 0.5 -rely 0.5 -anchor center
foreach win [list $w.area $w.area.l] {
    foreach b {1 2 3} {
	bind $win <Button-$b> [list apply {{b menu X Y} {
	    if {$::popupDemo(button$b)} {
		popupDemoPost $menu $X $Y
	    }
	}} $b $w.menu %X %Y]
    }
}

frame $w.options
foreach {var title} {height "Menu height" width "Menu width"} {
    labelframe $w.options.$var -text $title
    foreach {value text} {small "1/4 of the screen" large "2/3 of the screen"
	    huge "Larger than the screen"} {
	radiobutton $w.options.$var.$value -text $text \
		-variable popupDemo($var) -value $value
	pack $w.options.$var.$value -anchor w
    }
}
labelframe $w.options.buttons -text "Pop up with"
foreach {b text} {1 "Button 1" 2 "Button 2" 3 "Button 3"} {
    checkbutton $w.options.buttons.b$b -text $text \
	    -variable popupDemo(button$b)
    pack $w.options.buttons.b$b -side left
}
grid $w.options.height $w.options.width -sticky nsew -padx 4
grid $w.options.buttons - -sticky ew -padx 4 -pady 4

# The status line wraps its text to its width and has room for 2 lines, so
# that it does not change the size of the window.
label $w.status -textvariable popupDemo(status) -anchor nw -justify left \
	-relief sunken -bd 1 -width 1 -height 2
bind $w.status <Configure> {%W configure -wraplength [expr {%w - 8}]}

pack $w.area -side top -fill both -expand 1 -padx 4 -pady 4
pack $w.options -side top
pack $w.status -side top -fill x -padx 4 -pady 4
