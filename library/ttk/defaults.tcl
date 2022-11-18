#
# Settings for default theme.
#

namespace eval ttk::theme::default {
    variable colors
    array set colors {
	-frame			"#d9d9d9"
	-foreground		"#000000"
	-window			"#ffffff"
	-alternate		"#e8e8e8"
	-text   		"#000000"
	-activebg		"#ececec"
	-selectbg		"#4a6984"
	-selectfg		"#ffffff"
	-darker 		"#c3c3c3"
	-disabledfg		"#a3a3a3"
	-indicator		"#4a6984"
	-disabledindicator	"#a3a3a3"
	-altindicator		"#9fbdd8"
	-disabledaltindicator	"#c0c0c0"
    }

    # On X11, if the user specifies their own choice of colour scheme via X resources,
    # then set the colour palette based on the user's choice.
    if {[tk windowingsystem] eq "x11"} {
	foreach\
	    xResourceName\
                 { {	background		Background		}
                 {	foreground		Foreground		}
                 {	background		Background		}
                 {	background		Background		}
                 {	foreground		Foreground		}
                 {	activeBackground	ActiveBackground	}
                 {	selectBackground	SelectBackground	}
                 {	selectForeground	SelectForeground	}
                 {	troughColor		TroughColor		}
                 {	disabledForeground	DisabledForeground	}
                 {	selectBackground	SelectBackground	}
                 {	disabledForeground	DisabledForeground	}
                 {	selectBackground	SelectBackground	}
                 {	troughColor		TroughColor		}
                 {	windowColor		Background		} }\
	    colorName\
                 { -frame -foreground -window -alternate -text
                 -activebg -selectbg -selectfg
                 -darker -disabledfg -indicator
                 -disabledindicator -altindicator
                 -disabledaltindicator -window}\
	{
	    set color [eval option get . $xResourceName]
	    if {$color ne ""} {
                 set colors($colorName) $color
	    }
	}
    }
    # This array is used to match up the tk widget options with the
    # corresponding values in the 'colors' array.
    # This is used by tk_setPalette to apply the new palette
    # to the ttk widgets.
    variable colorOptionLookup
    array set colorOptionLookup {
	background		{-frame -window -alternate}
	foreground		{-foreground -text}
	activeBackground	-activebg
	selectBackground	{-selectbg -indicator -altindicator}
	selectForeground	-selectfg
	troughColor		{-darker -disabledaltindicator}
	disabledForeground	{-disabledfg -disabledindicator}
    }
}
# ttk::theme::default::reconfigureDefaultTheme --
# This procedure contains the definition of the 'default' theme itself.
# The theme definition is in a procedure, so it can be re-called
# when required, enabling tk_setPalette to set the colours
# of the ttk widgets.
#
# Arguments:
# None.

proc ttk::theme::default::reconfigureDefaultTheme {} {
    upvar ttk::theme::default::colors colors
    # The definition of the 'default' theme.

    ttk::style theme settings default {

	ttk::style configure "." \
	    -borderwidth 	1 \
	    -background 	$colors(-frame) \
	    -foreground 	$colors(-foreground) \
	    -troughcolor 	$colors(-darker) \
	    -font 		TkDefaultFont \
	    -selectborderwidth	1 \
	    -selectbackground	$colors(-selectbg) \
	    -selectforeground	$colors(-selectfg) \
	    -insertwidth 	1 \
	    -indicatordiameter	10 \
	    ;

	ttk::style map "." -background \
	    [list disabled $colors(-frame)  active $colors(-activebg)]
	ttk::style map "." -foreground \
	    [list disabled $colors(-disabledfg) !disabled $colors(-text)]
	ttk::style map "." -insertcolor \
	    [list !disabled $colors(-foreground)]
	ttk::style map "." -focuscolor \
	    [list !disabled $colors(-text)]

	ttk::style configure TButton \
	    -anchor center -padding "3 3" -width -9 \
	    -relief raised -shiftrelief 1
	ttk::style map TButton -relief [list {!disabled pressed} sunken]

	ttk::style configure TCheckbutton \
	    -indicatorcolor $colors(-window) -indicatorrelief sunken -padding 1
	ttk::style map TCheckbutton -indicatorcolor \
	    [list pressed $colors(-activebg)  \
			{!disabled alternate} $colors(-altindicator) \
			{disabled alternate} $colors(-disabledaltindicator) \
			{!disabled selected} $colors(-indicator) \
			{disabled selected} $colors(-disabledindicator)]
	ttk::style map TCheckbutton -indicatorrelief \
	    [list alternate raised]

	ttk::style configure TRadiobutton \
	    -indicatorcolor $colors(-window) -indicatorrelief sunken -padding 1
	ttk::style map TRadiobutton -indicatorcolor \
	    [list pressed $colors(-activebg)  \
			{!disabled alternate} $colors(-altindicator) \
			{disabled alternate} $colors(-disabledaltindicator) \
			{!disabled selected} $colors(-indicator) \
			{disabled selected} $colors(-disabledindicator)]
	ttk::style map TRadiobutton -indicatorrelief \
	    [list alternate raised]

	ttk::style configure TMenubutton \
	    -relief raised -padding "10 3"

	ttk::style configure TEntry \
	    -relief sunken -fieldbackground $colors(-window) -padding 1
	ttk::style map TEntry -fieldbackground \
	    [list readonly $colors(-frame) disabled $colors(-frame)]

	ttk::style configure TCombobox -arrowsize 12 -padding 1
	ttk::style map TCombobox -fieldbackground \
	    [list readonly $colors(-frame) disabled $colors(-frame) !disabled $colors(-window)] \
	    -arrowcolor [list disabled $colors(-disabledfg) !disabled $colors(-text)]

	ttk::style configure TSpinbox -arrowsize 10 -padding {2 0 10 0}
	ttk::style map TSpinbox -fieldbackground \
	    [list readonly $colors(-frame) disabled $colors(-frame) !disabled $colors(-window)] \
	    -arrowcolor [list disabled $colors(-disabledfg) !disabled $colors(-text)]

	ttk::style configure TLabelframe \
	    -relief groove -borderwidth 2

	ttk::style configure TScrollbar \
	    -width 12 -arrowsize 12
	ttk::style map TScrollbar \
	    -arrowcolor [list disabled $colors(-disabledfg) !disabled $colors(-text)]

	ttk::style configure TScale \
	    -sliderrelief raised
	ttk::style configure TProgressbar \
	    -background $colors(-selectbg)

	ttk::style configure TNotebook.Tab \
	    -padding {4 2} -background $colors(-darker)
	ttk::style map TNotebook.Tab \
	    -background [list selected $colors(-frame)]

	# Treeview.
	#
	ttk::style configure Heading -font TkHeadingFont -relief raised
	ttk::style configure Treeview \
            -background $colors(-window) \
            -stripedbackground $colors(-alternate) \
	    -fieldbackground $colors(-window) \
	    -foreground $colors(-text) ;
	ttk::style configure Treeview.Separator \
                -background $colors(-alternate)
	ttk::style map Treeview \
	    -background [list disabled $colors(-frame)\
                                selected $colors(-selectbg)] \
	    -foreground [list disabled $colors(-disabledfg) \
				selected $colors(-selectfg)]

	# Combobox popdown frame
	ttk::style layout ComboboxPopdownFrame {
	    ComboboxPopdownFrame.border -sticky nswe
	}
 	ttk::style configure ComboboxPopdownFrame \
	    -borderwidth 1 -relief solid

	#
	# Toolbar buttons:
	#
	ttk::style layout Toolbutton {
	    Toolbutton.border -children {
		Toolbutton.padding -children {
		    Toolbutton.label
		}
	    }
	}

	ttk::style configure Toolbutton \
	    -padding 2 -relief flat
	ttk::style map Toolbutton -relief \
	    [list disabled flat selected sunken pressed sunken active raised]
	ttk::style map Toolbutton -background \
	    [list pressed $colors(-darker)  active $colors(-activebg)]
    }
}

ttk::theme::default::reconfigureDefaultTheme
