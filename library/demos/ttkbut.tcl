# ttkbut.tcl --
#
# This demonstration script creates a toplevel window containing several
# simple Ttk widgets, such as labels, labelframes, buttons, checkbuttons,
# radiobuttons, a separator and a toggleswitch.

if {![info exists widgetDemo]} {
    error "This script should be run from the \"widget\" demo."
}

package require tk

set w .ttkbut
catch {destroy $w}
toplevel $w
wm title $w "Simple Ttk Widgets"
wm iconname $w "ttkbut"
positionWindow $w

ttk::label $w.msg -font $font -wraplength 4i -justify left -text "Ttk is the new Tk themed widget set. This is a Ttk themed label, and below are four groups of Ttk widgets in Ttk labelframes. The first group are all buttons that set the current application theme when pressed. The second group contains two sets of checkbuttons, with a separator widget between the sets. The third group has a collection of linked radiobuttons. Finally, the toggleswitch in the fourth labelframe controls whether all the themed widgets in this toplevel, except that labelframe and its children, are in the disabled state."
pack $w.msg -side top -fill x

## See Code / Dismiss
pack [addSeeDismiss $w.seeDismiss $w {enabled cheese tomato basil oregano happiness}]\
	-side bottom -fill x

## Add buttons for setting the theme
ttk::labelframe $w.buttons -text "Buttons"
foreach theme [lsort [ttk::themes]] {
    ttk::button $w.buttons.$theme -text $theme \
	    -command [list ttk::setTheme $theme]
    pack $w.buttons.$theme -pady 1.5p
}

## Set up the checkbutton group
ttk::labelframe $w.checks -text "Checkbuttons"
ttk::checkbutton $w.checks.c1 -text Cheese  -variable cheese
ttk::checkbutton $w.checks.c2 -text Tomato  -variable tomato
ttk::separator   $w.checks.sep
ttk::checkbutton $w.checks.c3 -text Basil   -variable basil
ttk::checkbutton $w.checks.c4 -text Oregano -variable oregano
### pack $w.checks.e $w.checks.sep1 $w.checks.c1 $w.checks.c2 $w.checks.sep2 \
	$w.checks.c3 $w.checks.c4   -fill x -pady 1.5p
pack $w.checks.c1 $w.checks.c2 $w.checks.sep $w.checks.c3 $w.checks.c4 \
	-fill x -pady 1.5p

## Set up the radiobutton group
ttk::labelframe $w.radios -text "Radiobuttons"
ttk::radiobutton $w.radios.r1 -text "Great" -variable happiness -value great
ttk::radiobutton $w.radios.r2 -text "Good" -variable happiness -value good
ttk::radiobutton $w.radios.r3 -text "OK" -variable happiness -value ok
ttk::radiobutton $w.radios.r4 -text "Poor" -variable happiness -value poor
ttk::radiobutton $w.radios.r5 -text "Awful" -variable happiness -value awful
pack $w.radios.r1 $w.radios.r2 $w.radios.r3 $w.radios.r4 $w.radios.r5 \
	-fill x -padx 3p -pady 1.5p

## Helper procedure for the toggleswitch
proc setState {rootWidget exceptThese value} {
    if {$rootWidget in $exceptThese} {
	return
    }
    ## Non-Ttk widgets (e.g. the toplevel) will fail, so make it silent
    catch {
	$rootWidget state $value
    }
    ## Recursively invoke on all children of this root that are in the same
    ## toplevel widget
    foreach w [winfo children $rootWidget] {
	if {[winfo toplevel $w] eq [winfo toplevel $rootWidget]} {
	    setState $w $exceptThese $value
	}
    }
}

## Set up the labelframe containing a label and a toggleswitch
ttk::labelframe $w.toggle -text Toggleswitch
ttk::label $w.toggle.l -text "Enable/disable widgets"
ttk::toggleswitch $w.toggle.sw -variable enabled -command {
    setState $w [list $w.toggle $w.toggle.l $w.toggle.sw] \
	    [expr {$enabled ? "!disabled" : "disabled"}]
}
set enabled 1
## See ttk_widget(n) for other possible state flags
pack $w.toggle.sw -side right -padx 3p -pady 1.5p
pack $w.toggle.l -side left -padx 3p -pady 1.5p

## Arrange things neatly
pack [ttk::frame $w.f] -fill both -expand 1
lower $w.f
grid $w.buttons $w.checks $w.radios -in $w.f -sticky nwe -pady 1.5p -padx 3p
grid $w.toggle -in $w.f -column 1 -columnspan 2 -sticky nwe -pady 1.5p -padx 3p
grid configure $w.buttons -rowspan 2
grid columnconfigure $w.f {0 1 2} -weight 1 -uniform yes
grid rowconfigure $w.f 1 -weight 1
