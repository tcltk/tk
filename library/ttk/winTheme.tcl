#
# Settings for 'winnative' theme.
#

namespace eval ttk::theme::winnative {

    namespace import ::tk::ScaleNum

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

	set padding [list [ScaleNum 2] [ScaleNum 4]]
	ttk::style configure TCheckbutton -padding $padding
	ttk::style configure TRadiobutton -padding $padding

	ttk::style configure TMenubutton \
	    -padding [list [ScaleNum 8] [ScaleNum 4]] \
	    -arrowsize [ScaleNum 3] -relief raised

	ttk::style configure TEntry \
	    -padding 2 -selectborderwidth 0 -insertwidth 1
	ttk::style map TEntry \
	    -fieldbackground \
		[list readonly SystemButtonFace disabled SystemButtonFace] \
	    -selectbackground [list !focus SystemWindow] \
	    -selectforeground [list !focus SystemWindowText] \
	    ;

	ttk::style configure TCombobox -padding [ScaleNum 2]
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

	set l [ScaleNum 2]; set r [ScaleNum 16]
        ttk::style configure TSpinbox -padding [list $l 0 $r 0]

	ttk::style configure TLabelframe -borderwidth 2 -relief groove

	ttk::style configure Toolbutton -relief flat \
	    -padding [list [ScaleNum 8] [ScaleNum 4]]
	ttk::style map Toolbutton -relief \
	    {disabled flat  selected sunken  pressed sunken  active raised}

	ttk::style configure TScale -groovewidth [ScaleNum 4]

	set m [ScaleNum 2]
	set margins [list $m $m $m 0]
	ttk::style configure TNotebook -tabmargins $margins
	ttk::style configure TNotebook.Tab \
	    -padding [list [ScaleNum 3] [ScaleNum 1]] -borderwidth 1
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
	    -barsize [ScaleNum 30] -thickness [ScaleNum 15]
    }

    unset padding l r m margins
}
