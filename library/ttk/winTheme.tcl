#
# Settings for 'winnative' theme.
#

namespace eval ttk::theme::winnative {

    ttk::style theme settings winnative {

	ttk::style configure "." \
	    -background systemButtonFace \
	    -foreground systemWindowText \
	    -selectforeground systemHighlightText \
	    -selectbackground systemHighlight \
	    -fieldbackground systemWindow \
	    -insertcolor systemWindowText \
	    -troughcolor systemScrollbar \
	    -font TkDefaultFont

	ttk::style map "." -foreground {disabled systemGrayText}
	ttk::style map "." -embossed {disabled 1}

	ttk::style configure TButton \
	    -anchor center -width -11 -relief raised -shiftrelief 1
	ttk::style map TButton -relief {{!disabled pressed} sunken}

	ttk::style configure TCheckbutton -padding {1.5p 3p}
	ttk::style configure TRadiobutton -padding {1.5p 3p}

	ttk::style configure TMenubutton \
	    -padding {6p 3p} -arrowsize 3.75p -relief raised

	ttk::style configure TEntry \
	    -padding 1.5p -insertwidth 0.75p
	ttk::style map TEntry \
	    -fieldbackground \
		{readonly systemButtonFace disabled systemButtonFace} \
	    -selectbackground {!focus {}} \
	    -selectforeground {!focus {}}

	ttk::style configure TCombobox -padding 1.5p -insertwidth 0.75p
	ttk::style map TCombobox \
	    -selectbackground [list !focus {}] \
	    -selectforeground [list !focus {}] \
	    -fieldbackground [list \
		readonly systemButtonFace \
		disabled systemButtonFace] \
	    -foreground	[list \
		disabled		systemGrayText \
		{readonly focus}	systemHighlightText \
	    ] \
	    -focusfill {{readonly focus} systemHighlight}

	ttk::style element create ComboboxPopdownFrame.border from default
	ttk::style configure ComboboxPopdownFrame \
	    -borderwidth 1 -relief solid

	ttk::style configure TSpinbox -padding {1.5p 0.75p 12p 0.75p} \
	    -insertwidth 0.75p

	# As in the native up-down control, each arrow takes half of the
	# height of the field (see SpinboxDoLayout).
	ttk::style layout TSpinbox {
	    Spinbox.field -sticky nswe -children {
		null -side right -sticky ns -children {
		    Spinbox.uparrow -side top -sticky e
		    Spinbox.downarrow -side bottom -sticky e
		}
		Spinbox.padding -sticky nswe -children {
		    Spinbox.textarea -sticky nswe
		}
	    }
	}

	ttk::style configure TLabelframe -borderwidth 2 -relief groove

	ttk::style configure Toolbutton -anchor center -justify center \
	    -padding {6p 3p} -relief flat -shiftrelief 1
	ttk::style map Toolbutton -relief \
	    {disabled flat  selected sunken  pressed sunken  active raised}

	ttk::style configure TScale -groovewidth 3p

	ttk::style configure TNotebook -tabmargins {2 2 2 0}
	ttk::style configure TNotebook.Tab -padding {2.25p 0.75p} -borderwidth 1
	ttk::style map TNotebook.Tab -expand {selected {2 2 2 0}}

	# Treeview
	ttk::style configure Heading \
	    -font TkHeadingFont -relief raised -padding {2.25p 1 2.25p 1} \
	    -indicatorsize 3p -indicatormargin {3p 1.5p 1.5p 1.5p}
	ttk::style configure Item -indicatorsize 6.75p \
	    -indicatormargin {1.5p 1.5p 3p 1.5p}
	ttk::style configure CheckTreeview.Item \
	    -padding {0 0.75p 0 0.75p}	;# because of Checkbutton.indicator
	ttk::style configure Row -focuscolor black \
	    -focussolid 0 -focusthickness 0 -padding 0
	ttk::style map Row -focusthickness [list focus 1]
	ttk::style configure Treeview -background systemWindow \
	    -stripedbackground system3dLight -indent 15p
	# The treeview uses the "background" state for
	# selected items when the widget has lost the focus.
	ttk::style map Treeview \
	    -background [list   disabled systemButtonFace \
				background systemScrollbar \
				selected systemHighlight] \
	    -foreground [list   disabled systemGrayText \
				background systemWindowText \
				selected systemHighlightText]

	ttk::style configure TProgressbar \
	    -background systemHighlight -borderwidth 0 \
	    -barsize 22.5p -thickness 11.25p
    }
}

# ttk::theme::winnative::configureNotebookStyle --
#
# Sets theme-specific option values for the ttk::notebook style $style and the
# style $style.Tab.  Invoked by ::ttk::configureNotebookStyle.

proc ttk::theme::winnative::configureNotebookStyle {style} {
    set tabPos [ttk::style lookup $style -tabposition {} nw]
    switch -- [string index $tabPos 0] {
	n {
	    ttk::style configure $style -tabmargins     {2 2 2 0}
	    ttk::style map $style.Tab -expand {selected {2 2 2 0}}
	}
	s {
	    ttk::style configure $style -tabmargins     {2 0 2 2}
	    ttk::style map $style.Tab -expand {selected {2 0 2 2}}
	}
	w {
	    ttk::style configure $style -tabmargins     {2 2 0 2}
	    ttk::style map $style.Tab -expand {selected {2 2 0 2}}
	}
	e {
	    ttk::style configure $style -tabmargins     {0 2 2 2}
	    ttk::style map $style.Tab -expand {selected {0 2 2 2}}
	}
	default {
	    ttk::style configure $style -tabmargins     {2 2 2 0}
	    ttk::style map $style.Tab -expand {selected {2 2 2 0}}
	}
    }
}
