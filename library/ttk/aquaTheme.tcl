#
# Aqua theme (OSX native look and feel)
#

namespace eval ttk::theme::aqua {
    ttk::style theme settings aqua {

	ttk::style configure . \
	    -font TkDefaultFont \
	    -background systemWindowBody \
	    -foreground systemLabelColor \
	    -selectbackground systemHighlight \
	    -selectforeground systemLabelColor \
	    -selectborderwidth 0 \
	    -insertwidth 1

	ttk::style map . \
	    -foreground {
		disabled systemLabelColor
		background systemLabelColor} \
	    -selectbackground {
		background systemHighlight
		!focus systemHighlightSecondary} \
	    -selectforeground {
		background systemLabelColor
		!focus systemDialogActiveText}

	# Buttons
	ttk::style configure TButton -anchor center -width -6 \
	    -foreground systemControlTextColor
	ttk::style map TButton \
	    -foreground {
		disabled systemDisabledControlTextColor}
	ttk::style map TCheckbutton \
	    -foreground {
		disabled systemDisabledControlTextColor}
	ttk::style map TRadiobutton \
	    -foreground {
		disabled systemDisabledControlTextColor}
	ttk::style map Toolbutton \
	    -foreground {
		disabled systemDisabledControlTextColor
	    }

	# Workaround for #1100117:
	# Actually, on Aqua we probably shouldn't stipple images in
	# disabled buttons even if it did work...
	ttk::style configure . -stipple {}

	# Notebook
	ttk::style configure TNotebook -tabmargins {10 0} -tabposition n
	ttk::style configure TNotebook -padding {18 8 18 17}
	ttk::style configure TNotebook.Tab -padding {12 3 12 2}
	ttk::style configure TNotebook.Tab -foreground white
	ttk::style map TNotebook.Tab \
	    -foreground {
		{background !disabled !selected} systemControlTextColor
		{background selected} black
		disabled systemDisabledControlTextColor
		!selected systemControlTextColor}

	# Combobox:
	# We do not have a drawing procedure for Dark Comboboxes.
	# This fakes the color in Dark Mode by using the system
	# background color for (light) inactive widgets, and uses a
	# white background for active Comboboxes, even in Dark Mode.
	ttk::style configure TCombobox -selectforeground black
	ttk::style map TCombobox \
	    -foreground {
		disabled systemDisabledControlTextColor
		focus black
		{} black
		!active systemControlTextColor
	    } \
	    -selectbackground {
		!focus white
	    }

	# Treeview:
	ttk::style configure Heading -font TkHeadingFont
	ttk::style configure Treeview -rowheight 18 -background White \
	    -foreground black
	ttk::style map Treeview \
	    -background {
		disabled systemDialogBackgroundInactive
		{selected background} systemHighlightSecondary
		selected systemHighlight} \
	    -foreground {
		!active black
	    }

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
