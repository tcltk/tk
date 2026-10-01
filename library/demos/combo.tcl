# combo.tcl --
#
# This demonstration script creates several combobox widgets.

if {![info exists widgetDemo]} {
    error "This script should be run from the \"widget\" demo."
}

package require Tk

set w .combo
catch {destroy $w}
toplevel $w
wm title $w "Combobox Demonstration"
wm iconname $w "combo"
positionWindow $w

ttk::label $w.msg -font $font -wraplength 5i -justify left -text "Four different\
	combo-boxes are displayed below. You can add characters to the first\
	one by pointing, clicking and typing, just as with an entry; pressing\
	Return will cause the current value to be added to the list that is\
	selectable from the drop-down list, and you can choose other values\
	by pressing the Down key, using the arrow keys to pick another one,\
	and pressing Return again. The second combo-box is fixed to a\
	particular value, and cannot be modified at all. The third one only\
	allows you to select values from its drop-down list of Australian\
	cities. The fourth one has a drop-down list with more values than\
	can fit on the screen; the list is shortened to fit on the screen and\
	can be scrolled."
pack $w.msg -side top -fill x

## See Code / Dismiss buttons
set btns [addSeeDismiss $w.buttons $w {firstValue secondValue ozCity longValue}]
pack $btns -side bottom -fill x

ttk::frame $w.f
pack $w.f -fill both -expand 1
set w $w.f

set australianCities {
    Canberra Sydney Melbourne Perth Adelaide Brisbane
    Hobart Darwin "Alice Springs"
}
set secondValue unchangable
set ozCity Sydney
set longValue 500
set longValues {}
for {set i 1} {$i <= 1000} {incr i} {
    lappend longValues $i
}

ttk::labelframe $w.c1 -text "Fully Editable"
ttk::combobox $w.c1.c -textvariable firstValue
ttk::labelframe $w.c2 -text Disabled
ttk::combobox $w.c2.c -textvariable secondValue -state disabled
ttk::labelframe $w.c3 -text "Defined List Only"
ttk::combobox $w.c3.c -textvariable ozCity -state readonly \
	-values $australianCities
ttk::labelframe $w.c4 -text "Long List"
ttk::combobox $w.c4.c -textvariable longValue -state readonly \
	-values $longValues -height 200
bind $w.c1.c <Return> {
    if {[%W get] ni [%W cget -values]} {
	%W configure -values [concat [%W cget -values] [list [%W get]]]
    }
}

pack $w.c1 $w.c2 $w.c3 $w.c4 -side top -pady 5 -padx 10
pack $w.c1.c -pady 5 -padx 10
pack $w.c2.c -pady 5 -padx 10
pack $w.c3.c -pady 5 -padx 10
pack $w.c4.c -pady 5 -padx 10
