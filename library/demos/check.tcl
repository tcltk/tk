# check.tcl --
#
# This demonstration script creates a toplevel window containing
# several checkbuttons.
#
# RCS: @(#) $Id: check.tcl,v 1.3 2003/08/20 23:02:18 hobbs Exp $

if {![info exists widgetDemo]} {
    error "This script should be run from the \"widget\" demo."
}

set w .check
catch {destroy $w}
toplevel $w
wm title $w "Checkbutton Demonstration"
wm iconname $w "check"
positionWindow $w

label $w.msg -font $font -wraplength 4i -justify left -text "Three checkbuttons are displayed below.  If you click on a button, it will toggle the button's selection state and set a Tcl variable to a value indicating the state of the checkbutton.  Click the \"See Variables\" button to see the current values of the variables."
pack $w.msg -side top

## See Code / Dismiss buttons
set btns [addSeeDismiss $w.buttons $w [list wipers brakes sober]]
pack $btns -side bottom -fill x

checkbutton $w.b1 -text "Wipers OK" -variable wipers -relief flat
checkbutton $w.b2 -text "Brakes OK" -variable brakes -relief flat
checkbutton $w.b3 -text "Driver Sober" -variable sober -relief flat
pack $w.b1 $w.b2 $w.b3 -side top -pady 2 -anchor w
