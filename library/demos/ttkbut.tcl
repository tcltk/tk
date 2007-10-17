# ttkbut.tcl --
#
# This demonstration script creates a toplevel window containing several
# simple Ttk widgets, such as labels, labelframes, buttons, checkbuttons and
# radiobuttons.
#
# RCS: @(#) $Id: ttkbut.tcl,v 1.1 2007/10/17 14:59:27 dkf Exp $

if {![info exists widgetDemo]} {
    error "This script should be run from the \"widget\" demo."
}

package require Tk
package require Ttk

set w .ttkbut
catch {destroy $w}
toplevel $w
wm title $w "Simple Ttk Widgets"
wm iconname $w "ttkbut"
positionWindow $w

ttk::label $w.msg -font $font -wraplength 4i -justify left -text "Ttk is the new Tk themed widget set. This is a Ttk themed label, and below are three groups of Ttk widgets in Ttk labelframes. The first group are all buttons that set the current application theme when pressed. The second group contains checkbuttons, with a separator widget between the first pair and the second. The third group has a collection of linked radiobuttons."
pack $w.msg -side top

## See Code / Dismiss
pack [addSeeDismiss $w.seeDismiss $w {cheese tomato basil oregano happyness}]\
	-side bottom -fill x

ttk::labelframe $w.buttons -text "Buttons"
foreach theme [ttk::themes] {
    ttk::button $w.buttons.$theme -text $theme \
	    -command [list ttk::setTheme $theme]
    pack $w.buttons.$theme -pady 2
}

ttk::labelframe $w.checks -text "Checkbuttons"
ttk::checkbutton $w.checks.c1 -text Cheese  -variable cheese
ttk::checkbutton $w.checks.c2 -text Tomato  -variable tomato
ttk::separator   $w.checks.sep
ttk::checkbutton $w.checks.c3 -text Basil   -variable basil
ttk::checkbutton $w.checks.c4 -text Oregano -variable oregano
pack $w.checks.c1 $w.checks.c2 $w.checks.sep $w.checks.c3 $w.checks.c4 \
	-fill x -pady 2

ttk::labelframe $w.radios -text "Radiobuttons"
ttk::radiobutton $w.radios.r1 -text "Great" -variable happyness -value great
ttk::radiobutton $w.radios.r2 -text "Good" -variable happyness -value good
ttk::radiobutton $w.radios.r3 -text "OK" -variable happyness -value ok
ttk::radiobutton $w.radios.r4 -text "Poor" -variable happyness -value poor
ttk::radiobutton $w.radios.r5 -text "Awful" -variable happyness -value awful
pack $w.radios.r1 $w.radios.r2 $w.radios.r3 $w.radios.r4 $w.radios.r5 \
	-fill x -padx 3 -pady 2

pack [ttk::frame $w.f] -fill both -expand 1
lower $w.f
grid $w.buttons $w.checks $w.radios -in $w.f -sticky nwe -pady 2 -padx 3
grid columnconfigure $w.f {0 1 2} -weight 1 -uniform yes
