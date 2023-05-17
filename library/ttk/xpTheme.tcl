#
# Settings for 'xpnative' theme
#

namespace eval ttk::theme::xpnative {

    ttk::style theme settings xpnative {

	ttk::style configure . \
	    -background SystemButtonFace \
	    -foreground SystemWindowText \
	    -selectforeground SystemHighlightText \
	    -selectbackground SystemHighlight \
	    -insertcolor SystemWindowText \
	    -font TkDefaultFont

	ttk::style map "." \
	    -foreground [list disabled SystemGrayText]

	ttk::style configure TButton -anchor center -padding 0.75p -width -11
	ttk::style configure TRadiobutton -padding 1.5p
	ttk::style configure TCheckbutton -padding 1.5p
	ttk::style configure TMenubutton -padding {6p 3p}

	ttk::style configure TNotebook -tabmargins {1.5p 1.5p 1.5p 0}
	ttk::style map TNotebook.Tab \
	    -expand {selected {1.5p 1.5p 1.5p 1.5p}}

	ttk::style configure TLabelframe.Label -foreground "#0046d5"

	# OR: -padding {3 3 3 6}, which some apps seem to use.
	ttk::style configure TEntry -padding {2 2 2 4}
	ttk::style map TEntry \
	    -selectbackground [list !focus SystemWindow] \
	    -selectforeground [list !focus SystemWindowText]
	ttk::style configure TCombobox -padding 1.5p
	ttk::style map TCombobox \
	    -selectbackground [list !focus SystemWindow] \
	    -selectforeground [list !focus SystemWindowText] \
	    -foreground	[list \
		disabled		SystemGrayText \
		{readonly focus}	SystemHighlightText \
	    ] \
	    -focusfill	[list {readonly focus} SystemHighlight]

	ttk::style configure TSpinbox -padding {1.5p 0 10.5p 0}
	ttk::style map TSpinbox \
	    -selectbackground [list !focus SystemWindow] \
	    -selectforeground [list !focus SystemWindowText]

	ttk::style configure Toolbutton -padding 3p

	# Treeview:
	ttk::style configure Heading -font TkHeadingFont -relief raised
	ttk::style configure Item \
	    -indicatormargins {1.5p 1.5p 3p 1.5p}
	ttk::style configure Treeview -background SystemWindow \
	    -stripedbackground System3dLight -indent 15p
	ttk::setTreeviewRowHeight
	ttk::style map Treeview \
	    -background [list   disabled SystemButtonFace \
				selected SystemHighlight] \
	    -foreground [list   disabled SystemGrayText \
				selected SystemHighlightText]
    }
}
