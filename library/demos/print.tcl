# print.tcl --
#
# This demonstration script showcases the tk print commands.
#

if {![info exists widgetDemo]} {
    error "This script should be run from the \"widget\" demo."
}

set w .print
destroy $w
toplevel $w
wm title $w "Printing Demonstration"
positionWindow $w

pack [label $w.l -text "This demonstration showcases
        the tk print command. Clicking the buttons below
        print the data from the canvas and text widgets
        using platform-native dialogs."] -side top

pack [frame $w.m] -fill both -expand yes -side top

set c [canvas $w.m.c -bg white]
pack $c -fill both -expand no -side left

$c create rectangle 10 10 200 50 -fill blue -outline black
$c create oval 10 60 200 110 -fill green
$c create text 110 120   -anchor n -font {Helvetica 12}  \
	-text "A short demo of simple canvas elements."

set txt {
Tcl, or Tool Command Language, is an open-source multi-purpose C library which includes a powerful dynamic scripting language. Together they provide ideal cross-platform development environment for any programming project. It has served for decades as an essential system component in organizations ranging from NASA to Cisco Systems, is a must-know language in the fields of EDA, and powers companies such as FlightAware and F5 Networks.

Tcl is fit for both the smallest and largest programming tasks, obviating the need to decide whether it is overkill for a given job or whether a system written in Tcl will scale up as needed. Wherever a shell script might be used Tcl is a better choice, and entire web ecosystems and mission-critical control and testing systems have also been written in Tcl. Tcl excels in all these roles due to the minimal syntax of the language, the unique programming paradigm exposed at the script level, and the careful engineering that has gone into the design of the Tcl internals.
}

set t [text $w.m.t -wrap word]
pack $t -side right -fill both -expand no
$t insert end $txt

pack [frame $w.f] -side top -fill both -expand no
pack [button $w.f.b -text "Print Canvas" -command [list tk print canvas $w.c]]  -expand no
pack [button $w.f.x -text "Print Text" -command [list tk print text $w.t]]   -expand no

## See Code / Dismiss buttons
pack [addSeeDismiss $w.buttons $w] -side bottom -fill x

