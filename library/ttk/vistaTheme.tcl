#
# Settings for Microsoft Windows Vista and Server 2008
#

# The Vista theme can only be defined on Windows Vista and above. The theme
# is created in C due to the need to assign a theme-enabled function for
# detecting when themeing is disabled. On systems that cannot support the
# Vista theme, there will be no such theme created and we must not
# evaluate this script.

if {"vista" ni [ttk::style theme names]} {
    return
}

namespace eval ttk::theme::vista {

    ttk::style theme settings vista {

	ttk::style configure . \
	    -background SystemButtonFace \
	    -foreground SystemButtonText \
	    -selectforeground SystemHighlightText \
	    -selectbackground SystemHighlight \
	    -insertcolor SystemWindowText \
	    -font TkDefaultFont

	ttk::style map . -foreground [list disabled SystemGrayText]

	# Buttons
	ttk::style configure TButton -anchor center -padding 0.75p -width -11
	ttk::style configure TRadiobutton -padding 1.5p
	ttk::style configure TCheckbutton -padding 1.5p
	ttk::style configure TMenubutton -padding {6p 3p}
	ttk::style configure Toolbutton -padding 3p

	# Combobox
	ttk::style configure TCombobox -padding 1 \
	    -foreground SystemWindowText
	ttk::style map TCombobox \
	    -selectbackground [list !focus SystemWindow] \
	    -selectforeground [list !focus SystemWindowText] \
	    -focusfill	[list {readonly focus} SystemHighlight] \
	    -foreground	[list disabled SystemGrayText readonly SystemGrayText]

	# Vista.Combobox droplist frame
	ttk::style configure ComboboxPopdownFrame.background -border 1

	# Entry
	ttk::style configure TEntry -padding 1 \
	    -foreground SystemWindowText
	ttk::style configure TEntry.textarea -padding {0 0 10 0}
	ttk::style map TEntry \
	    -selectbackground [list !focus SystemWindow] \
	    -selectforeground [list !focus SystemWindowText] \
	    -foreground	[list disabled SystemGrayText readonly SystemGrayText]

	# Frame
	ttk::style configure TLabelframe.Label -foreground SystemButtonText

	# Notebook
	ttk::style configure TNotebook -tabmargins {6 2 6 2}
	ttk::style map TNotebook.Tab -expand {selected {2 2 2 2}}

	# Progressbar
	ttk::style configure Horizontal.Progressbar.pbar -padding 8
	ttk::style configure Vertical.Progressbar.pbar -padding 8

	# Scale

	# Scrollbar

	# Spinbox
	ttk::style configure TSpinbox -padding 1 \
	    -foreground SystemWindowText
	ttk::style map TSpinbox \
	    -selectbackground [list !focus SystemWindow] \
	    -selectforeground [list !focus SystemWindowText] \
	    -foreground	[list disabled SystemGrayText readonly SystemGrayText]

	# Treeview
	ttk::style configure Heading -font TkHeadingFont \
	    -padding {0 1.5p 0 1.5p}

	ttk::style configure Item -padding {3p 0 0 0}
	ttk::style configure CheckTreeview.Item \
	    -padding {3p 0.75p 0 0.75p}	;# because of Checkbutton.indicator

	ttk::style configure Row -focuscolor black \
	    -focussolid 1 -focusthickness 0 -padding 0
	ttk::style map Row -focusthickness [list focus 1]

	ttk::style configure Treeview -indent 15p \
	    -stripedbackground SystemInactiveBorder
	ttk::style map Treeview \
	    -foreground	[list disabled SystemGrayText]

	ttk::style configure Treeview.Separator \
	    -background System3dLight
    }
}

# ttk::theme::vista::configureNotebookStyle --
#
# Sets theme-specific option values for the ttk::notebook style $style and the
# style $style.Tab.  Invoked by ::ttk::configureNotebookStyle.

proc ttk::theme::vista::configureNotebookStyle {style} {
    set tabPos [ttk::style lookup $style -tabposition {} nw]
    switch -- [string index $tabPos 0] {
	n {
	    ttk::style configure $style -tabmargins     {2 2 2 0}
	    ttk::style map $style.Tab -expand {selected {2 2 2 2}}
	}
	s {
	    ttk::style configure $style -tabmargins     {2 0 2 2}
	    ttk::style map $style.Tab -expand {selected {2 2 2 2}}
	}
	w {
	    ttk::style configure $style -tabmargins     {2 2 0 2}
	    ttk::style map $style.Tab -expand {selected {2 2 2 2}}
	}
	e {
	    ttk::style configure $style -tabmargins     {0 2 2 2}
	    ttk::style map $style.Tab -expand {selected {2 2 2 2}}
	}
	default {
	    ttk::style configure $style -tabmargins     {2 2 2 0}
	    ttk::style map $style.Tab -expand {selected {2 2 2 2}}
	}
    }
}
