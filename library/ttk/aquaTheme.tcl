#
# Aqua theme (OSX native look and feel)
#

namespace eval ttk::theme::aqua {
    ttk::style theme settings aqua {

	ttk::style configure . \
	    -font TkDefaultFont \
	    -background systemWindowBody \
	    -foreground systemTtkForeground \
	    -selectbackground systemHighlight \
	    -selectforeground systemTtkForeground \
	    -selectborderwidth 0 \
	    -insertwidth 1

	ttk::style map . \
	    -foreground {disabled systemTtkForeground
		    background systemTtkForeground} \
	    -selectbackground {background systemHighlightSecondary
		    !focus systemHighlightSecondary} \
	    -selectforeground {background systemTtkForeground
		    !focus systemDialogActiveText}

	# Workaround for #1100117:
	# Actually, on Aqua we probably shouldn't stipple images in
	# disabled buttons even if it did work...
	ttk::style configure . -stipple {}

	ttk::style configure TButton -anchor center -width -6

	ttk::style configure Toolbutton -padding 4

	ttk::style configure TNotebook -tabmargins {10 0} -tabposition n
	ttk::style configure TNotebook -padding {18 8 18 17}
	ttk::style configure TNotebook.Tab -padding {12 3 12 2}
	ttk::style configure TNotebook.Tab -foreground white
	ttk::style map TNotebook.Tab -foreground {
	    {background !disabled} black
	    disabled darkgray
	    !selected black}

	# Combobox:
	ttk::style configure TCombobox -postoffset {5 -2 -10 0}

	# Treeview:
	ttk::style configure Heading -font TkHeadingFont
	ttk::style configure Treeview -rowheight 18 -background White
	ttk::style map Treeview \
	    -background [list disabled systemDialogBackgroundInactive \
				{selected background} systemHighlightSecondary \
				selected systemHighlight] \
	    -foreground [list disabled black \
				selected black]

	# Enable animation for ttk::progressbar widget:
	ttk::style configure TProgressbar -period 100 -maxphase 255

	# For Aqua, labelframe labels should appear outside the border,
	# with a 14 pixel inset and 4 pixels spacing between border and label
	# (ref: Apple Human Interface Guidelines / Controls / Grouping Controls)
	#
	ttk::style configure TLabelframe \
		-labeloutside true -labelmargins {14 0 14 4}

	# TODO: panedwindow sashes should be 9 pixels (HIG:Controls:Split Views)
    }
}
