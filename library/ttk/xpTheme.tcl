#
# Settings for 'xpnative' theme
#

namespace eval ttk::theme::xpnative {

    namespace import ::tk::ScaleNum

    ttk::style theme settings xpnative {

	ttk::style configure . \
	    -background SystemButtonFace \
	    -foreground SystemWindowText \
	    -selectforeground SystemHighlightText \
	    -selectbackground SystemHighlight \
	    -insertcolor SystemWindowText \
	    -font TkDefaultFont \
	    ;

	ttk::style map "." \
	    -foreground [list disabled SystemGrayText] \
	    ;

	ttk::style configure TButton -anchor center -padding [ScaleNum 1] \
	    -width -11
	ttk::style configure TRadiobutton -padding [ScaleNum 2]
	ttk::style configure TCheckbutton -padding [ScaleNum 2]
	ttk::style configure TMenubutton \
	    -padding [list [ScaleNum 8] [ScaleNum 4]]

	set m [ScaleNum 2]
	ttk::style configure TNotebook -tabmargins [list $m $m $m 0]
	ttk::style map TNotebook.Tab \
	    -expand [list selected [list $m $m $m $m]]

	ttk::style configure TLabelframe.Label -foreground "#0046d5"

	# OR: -padding {3 3 3 6}, which some apps seem to use.
	ttk::style configure TEntry -padding {2 2 2 4}
	ttk::style map TEntry \
	    -selectbackground [list !focus SystemWindow] \
	    -selectforeground [list !focus SystemWindowText] \
	    ;
	ttk::style configure TCombobox -padding [ScaleNum 2]
	ttk::style map TCombobox \
	    -selectbackground [list !focus SystemWindow] \
	    -selectforeground [list !focus SystemWindowText] \
	    -foreground	[list \
		disabled		SystemGrayText \
		{readonly focus}	SystemHighlightText \
	    ] \
	    -focusfill	[list {readonly focus} SystemHighlight] \
	    ;

	set l [ScaleNum 2]; set r [ScaleNum 14]
	ttk::style configure TSpinbox -padding [list $l 0 $r 0]
	ttk::style map TSpinbox \
	    -selectbackground [list !focus SystemWindow] \
	    -selectforeground [list !focus SystemWindowText] \
	    ;

	ttk::style configure Toolbutton -padding [ScaleNum 4]

	# Treeview:
	ttk::style configure Heading -font TkHeadingFont -relief raised
	ttk::style configure Treeview -background SystemWindow \
                -stripedbackground System3dLight
	ttk::style map Treeview \
	    -background [list   disabled SystemButtonFace \
				selected SystemHighlight] \
	    -foreground [list   disabled SystemGrayText \
				selected SystemHighlightText];
    }

    unset m l r
}
