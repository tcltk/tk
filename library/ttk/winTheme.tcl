#
# Settings for 'winnative' theme.
#

namespace eval ttk::theme::winnative {

    ttk::style theme settings winnative {

	ttk::style configure "." \
	    -background SystemButtonFace \
	    -foreground SystemWindowText \
	    -selectforeground SystemHighlightText \
	    -selectbackground SystemHighlight \
	    -fieldbackground SystemWindow \
	    -insertcolor SystemWindowText \
	    -troughcolor SystemScrollbar \
	    -font TkDefaultFont \
	    ;

	ttk::style map "." -foreground [list disabled SystemGrayText] ;
        ttk::style map "." -embossed [list disabled 1] ;

	ttk::style configure TButton \
	    -anchor center -width -11 -relief raised -shiftrelief 1
	ttk::style map TButton -relief {{!disabled pressed} sunken}

	set padding {1.5p 3p}
	ttk::style configure TCheckbutton -padding $padding
	ttk::style configure TRadiobutton -padding $padding

	ttk::style configure TMenubutton \
	    -padding {6p 3p} -arrowsize 2.25p -relief raised

	ttk::style configure TEntry \
	    -padding 2 -selectborderwidth 0 -insertwidth 1
	ttk::style map TEntry \
	    -fieldbackground \
		[list readonly SystemButtonFace disabled SystemButtonFace] \
	    -selectbackground [list !focus SystemWindow] \
	    -selectforeground [list !focus SystemWindowText] \
	    ;

	ttk::style configure TCombobox -padding 1.5p
	ttk::style map TCombobox \
	    -selectbackground [list !focus SystemWindow] \
	    -selectforeground [list !focus SystemWindowText] \
	    -fieldbackground [list \
		readonly SystemButtonFace \
		disabled SystemButtonFace] \
	    -foreground	[list \
		disabled		SystemGrayText \
		{readonly focus}	SystemHighlightText \
	    ] \
	    -focusfill	[list {readonly focus} SystemHighlight] \
	    ;

	ttk::style element create ComboboxPopdownFrame.border from default
	ttk::style configure ComboboxPopdownFrame \
	    -borderwidth 1 -relief solid

	ttk::style configure TSpinbox -padding {1.5p 0 12p 0}

	ttk::style configure TLabelframe -borderwidth 2 -relief groove

	ttk::style configure Toolbutton -relief flat -padding {6p 3p}
	ttk::style map Toolbutton -relief \
	    {disabled flat  selected sunken  pressed sunken  active raised}

	ttk::style configure TScale -groovewidth 3p

	set m 1.5p
	set margins [list $m $m $m 0]
	ttk::style configure TNotebook -tabmargins $margins
	ttk::style configure TNotebook.Tab \
	    -padding {2.25p 0.75p} -borderwidth 1
	ttk::style map TNotebook.Tab -expand [list selected $margins]

	# Treeview:
	ttk::style configure Heading -font TkHeadingFont -relief raised
	ttk::style configure Treeview -background SystemWindow \
                -stripedbackground System3dLight
	ttk::style map Treeview \
	    -background [list   disabled SystemButtonFace \
				selected SystemHighlight] \
	    -foreground [list   disabled SystemGrayText \
				selected SystemHighlightText]

        ttk::style configure TProgressbar \
	    -background SystemHighlight -borderwidth 0 \
	    -barsize 22.5p -thickness 11.25p
    }

    unset padding m margins
}
