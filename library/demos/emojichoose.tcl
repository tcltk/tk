# emojichoose.tcl --
#
# Show off the stock emoji picker dialog

if {![info exists widgetDemo]} {
    error "This script should be run from the \"widget\" demo."
}

package require tk

set w .emojichoose
catch {destroy $w}
toplevel $w
wm title $w "Emoji Selection Dialog"
wm iconname $w "emojichooser"
positionWindow $w



label $w.msg -font $font -wraplength 4i -anchor w -justify left \
    -text "This is a demonstration of Tk's support for emojis.\
	To access the emoji picker, use the following platform-specific \
	keyboard shortcuts:\n\n\
	macOS: Command-Control-Space\n\
	Windows: Windows-.\n\
	X11: Control-.\n\n\The emoji picker will appear near the text or entry widget\
	below and will insert the selected emoji."

pack $w.msg -side top

## See Code / Dismiss buttons
set btns [addSeeDismiss $w.buttons $w]
pack $btns -side bottom -fill x

## The frame that will contain the widgets.
pack [frame $w.f] -side bottom -expand 1 -fill both -padx 2m -pady 1m

text $w.f.msg -width 40 -height 6 -borderwidth 0 
entry $w.f.e -width 40
pack $w.f.msg $w.f.e -expand 1 -fill both -padx 2m -pady 1m
