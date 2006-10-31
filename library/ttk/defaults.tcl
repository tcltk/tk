#
# $Id: defaults.tcl,v 1.1 2006/10/31 01:42:27 hobbs Exp $
#
# Ttk widget set: Default theme
#

namespace eval ttk {
    # XXX do we want to separate Tk version from theme version?
    package provide ttk::theme::default $::tk_version

    variable colors
    array set colors {
	-frame		"#d9d9d9"
	-activebg	"#ececec"
	-selectbg	"#4a6984"
	-selectfg	"#ffffff"
	-darker 	"#c3c3c3"
	-disabledfg	"#a3a3a3"
	-indicator	"#4a6984"
    }

    style theme settings default {

	style configure "." \
	    -borderwidth 	1 \
	    -background 	$colors(-frame) \
	    -foreground 	black \
	    -troughcolor 	$colors(-darker) \
	    -font 		TkDefaultFont \
	    -selectborderwidth	1 \
	    -selectbackground	$colors(-selectbg) \
	    -selectforeground	$colors(-selectfg) \
	    -insertwidth 	1 \
	    -indicatordiameter	10 \
	    ;

	style map "." -background \
	    [list disabled $colors(-frame)  active $colors(-activebg)]
	style map "." -foreground \
	    [list disabled $colors(-disabledfg)]

	style configure TButton \
	    -padding "3 3" -width -9 -relief raised -shiftrelief 1
	style map TButton -relief [list {!disabled pressed} sunken]

	style configure TCheckbutton \
	    -indicatorcolor "#ffffff" -indicatorrelief sunken -padding 1
	style map TCheckbutton -indicatorcolor \
	    [list pressed $colors(-activebg)  selected $colors(-indicator)]

	style configure TRadiobutton \
	    -indicatorcolor "#ffffff" -indicatorrelief sunken -padding 1
	style map TRadiobutton -indicatorcolor \
	    [list pressed $colors(-activebg)  selected $colors(-indicator)]

	style configure TMenubutton -relief raised -padding "10 3"

	style configure TEntry -relief sunken -fieldbackground white -padding 1
	style map TEntry -fieldbackground \
	    [list readonly $colors(-frame) disabled $colors(-frame)]

	style configure TCombobox -arrowsize 12 -padding 1
	style map TCombobox -fieldbackground \
	    [list readonly $colors(-frame) disabled $colors(-frame)]

	style configure TLabelframe -relief groove -borderwidth 2

	style configure TScrollbar -width 12 -arrowsize 12
	style map TScrollbar -arrowcolor [list disabled $colors(-disabledfg)]

	style configure TScale -sliderrelief raised
	style configure TProgressbar -background $colors(-selectbg)

	style configure TNotebook.Tab \
	    -padding {4 2} -background $colors(-darker)
	style map TNotebook.Tab -background [list selected $colors(-frame)]

	#
	# Toolbar buttons:
	#
	style layout Toolbutton {
	    Toolbutton.border -children {
		Toolbutton.padding -children {
		    Toolbutton.label
		}
	    }
	}

	style configure Toolbutton -padding 2 -relief flat
	style map Toolbutton -relief \
	    {disabled flat selected sunken pressed sunken active raised}
	style map Toolbutton -background \
	    [list pressed $colors(-darker)  active $colors(-activebg)]
    }
}
