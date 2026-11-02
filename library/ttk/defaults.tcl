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
	-text			"#000000"
	-activebg		"#ececec"
	-selectbg		"#4a6984"
	-selectfg		"#ffffff"
	-darker			"#c3c3c3"
	-disabledfg		"#a3a3a3"
	-indicator		"#4a6984"
	-disabledindicator	"#a3a3a3"
	-pressedindicator	"#5895bc"
	-inactiveselectbg	"#c3c3c3"
	-inactiveselectfg	"#ffffff"
    }

    # DeriveMissingColors --
    #
    # seedColors --
    #
    # Sets the whole colour palette.  Every colour the option database
    # answers for is taken from it, and every colour it does not answer for
    # is computed from those which it did.
    #
    # Reading them from the option database, rather than being handed them,
    # is what makes its priorities govern the ttk widgets as they govern the
    # others: an X resource of the user's outranks anything Tk itself put
    # there - including the colours Tk takes from the desktop, which
    # ::tk::ApplyDesktopColorScheme writes at the widgetDefault priority.
    #
    # Computing the rest is necessary because the database rarely answers
    # for all of it.  A desktop which translates its theme into X resources
    # typically defines only background and foreground, and a user editing
    # .Xdefaults by hand often defines fewer still: see ticket [807b2f8916].
    # Whatever is left would otherwise keep a value chosen for some other
    # colour scheme - a dark background beside Tk's near-white active
    # background being the reported symptom, the text of a hovered button
    # becoming illegible.
    #
    # Each colour is computed from the one it belongs with: what goes with
    # the frame from the frame, what goes with the selection from the
    # selection.  Where neither was supplied there is nothing better than
    # the palette's own values, which go with one another already.
    #
    # The selection colour itself is never computed.  It is an accent, not a
    # shade of the frame: nothing can derive an accent from a grey
    # background, and the palette's own blue goes with a light frame and a
    # dark one alike.  A desktop which has an accent colour of its own
    # supplies it through the option database like any other colour.
    #
    # A caller which has colours of its own to impose, rather than leaving
    # them to the database, passes them in: see ::tk::SetAppearance, which
    # keeps the palette's accent across a change of background.
    #
    # Arguments:
    # supplied -	a dictionary of colour names and values, which the
    #		option database is not consulted for.

    proc seedColors {{supplied {}}} {
	variable colors

	foreach \
		xResourceName {
		    {	background		Background		}
		    {	foreground		Foreground		}
		    {	foreground		Foreground		}
		    {	activeBackground	ActiveBackground	}
		    {	selectBackground	SelectBackground	}
		    {	selectForeground	SelectForeground	}
		    {	troughColor		TroughColor		}
		    {	disabledForeground	DisabledForeground	}
		    {	selectBackground	SelectBackground	}
		    {	disabledForeground	DisabledForeground	}
		    {	windowColor		Background		} } \
		colorName {
		    -frame -foreground -text
		    -activebg -selectbg -selectfg
		    -darker -disabledfg -indicator
		    -disabledindicator -window } {
	    if {[dict exists $supplied $colorName]} {
		continue
	    }
	    set color [eval option get . $xResourceName]
	    if {$color ne ""} {
		dict set supplied $colorName $color
	    }
	}
	array set colors $supplied

	#
	# What goes with the frame.  These are the rules tk_setPalette
	# applies to the classic widgets, which keeps the two in agreement.
	#

	if {[dict exists $supplied -frame]} {
	    set frame $colors(-frame)

	    # A background given without a foreground is the common case: Tk's
	    # own foreground is black, which a dark background would hide, so
	    # choose one which can be read against the background - as
	    # tk_setPalette does when it is given a background and nothing
	    # else.
	    if {![dict exists $supplied -foreground]} {
		set colors(-foreground) [::tk::ContrastColor $frame]
	    }
	    if {![dict exists $supplied -text]} {
		set colors(-text) $colors(-foreground)
	    }
	    set foreground $colors(-foreground)

	    if {![dict exists $supplied -activebg]} {
		set colors(-activebg) [::tk::Lighten $frame]
	    }
	    if {![dict exists $supplied -darker]} {
		set colors(-darker) [::tk::Darken $frame 90]
	    }
	    if {![dict exists $supplied -disabledfg]} {
		set colors(-disabledfg) [::tk::BlendColors $frame $foreground]
	    }
	    if {![dict exists $supplied -disabledindicator]} {
		set colors(-disabledindicator) $colors(-disabledfg)
	    }

	    # The field of an entry or of a list is paper to the frame's
	    # desk: white against a light frame, and darker than the frame
	    # against a dark one, there being nothing lighter to go to.  It
	    # is computed rather than read from a resource of its own,
	    # because a resource which no user ever sets cannot be outranked
	    # by one, and the user's own background would then be unable to
	    # reach it.
	    if {![dict exists $supplied -window]} {
		if {[::tk::ContrastColor $frame] eq "white"} {
		    set colors(-window) [::tk::Darken $frame 72]
		} else {
		    set colors(-window) #ffffff
		}
	    }
	    if {![dict exists $supplied -alternate]} {
		set colors(-alternate) \
			[::tk::BlendColors $colors(-window) $frame 1 1]
	    }
	}

	#
	# What goes with the selection.
	#

	if {[dict exists $supplied -selectbg]} {
	    # A selection colour without a foreground to put on it, or
	    # without the lighter shade a pressed indicator takes, would
	    # leave those at values chosen for a different selection colour.
	    if {![dict exists $supplied -selectfg]} {
		set colors(-selectfg) [::tk::ContrastColor $colors(-selectbg)]
	    }
	    if {![dict exists $supplied -pressedindicator]} {
		set colors(-pressedindicator) \
			[::tk::Lighten $colors(-indicator)]
	    }
	}

	#
	# What goes with both: the colours a selection takes once its window
	# has lost the focus.  Tk used to show it in the trough colour, a
	# shade of the frame, while leaving the text in the selection's own
	# foreground - two colours chosen for different purposes and under no
	# obligation to agree.  Against a dark frame the one is nearly the
	# frame itself and the other can be black, which is how a selected
	# row came to be invisible.  Dimming the selection towards the frame
	# instead keeps it recognisable as a selection, and the foreground
	# then follows from it.
	#

	if {[dict exists $supplied -frame] || [dict exists $supplied -selectbg]} {
	    set colors(-inactiveselectbg) \
		    [::tk::BlendColors $colors(-selectbg) $colors(-frame) 1 1]
	    set colors(-inactiveselectfg) \
		    [::tk::ContrastColor $colors(-inactiveselectbg)]
	}
    }

    if {[tk windowingsystem] eq "x11"} {
	seedColors
    }

    # This array is used to match up the tk widget options with
    # the corresponding values in the 'colors' array.
    # This is used by tk_setPalette to apply the new palette
    # to the ttk widgets.
    variable colorOptionLookup
    array set colorOptionLookup {
	background		{-frame -window -alternate}
	foreground		{-foreground -text}
	activeBackground	-activebg
	selectBackground	{-selectbg -indicator -pressedindicator}
	selectForeground	-selectfg
	troughColor		-darker
	disabledForeground	{-disabledfg -disabledindicator}
    }
}

# ttk::theme::default::reconfigureDefaultTheme --
#
# This procedure contains the definition of the 'default' theme itself.
# The theme definition is in a procedure, so it can be re-called when
# required, enabling tk_setPalette to set the colours of the ttk widgets.
#
# Arguments:
# None.

proc ttk::theme::default::reconfigureDefaultTheme {} {
    # Note: a 'variable' declaration, not 'upvar ttk::theme::default::colors',
    # because the latter resolves the name relative to the caller's namespace,
    # and so only works for a caller in the global namespace.
    variable colors

    # The definition of the 'default' theme.

    ttk::style theme settings default {

	ttk::style configure "." \
	    -borderwidth	1 \
	    -background		$colors(-frame) \
	    -foreground		$colors(-foreground) \
	    -troughcolor	$colors(-darker) \
	    -font		TkDefaultFont \
	    -selectbackground	$colors(-selectbg) \
	    -selectforeground	$colors(-selectfg) \
	    -insertwidth	0.75p \
	    -insertcolor	$colors(-foreground) \
	    -focuscolor		$colors(-text)

	ttk::style map "." -background \
	    [list disabled $colors(-frame)  active $colors(-activebg)]
	ttk::style map "." -foreground \
	    [list disabled $colors(-disabledfg)]

	ttk::style configure TButton \
	    -anchor center -padding 2.25p -width -9 \
	    -relief raised -shiftrelief 1
	ttk::style map TButton -relief [list {!disabled pressed} sunken]

	foreach style {TCheckbutton TRadiobutton} {
	    ttk::style configure $style \
		-indicatorbackground $colors(-window) \
		-indicatorforeground $colors(-selectfg) \
		-indicatormargin {0 1.5p 3p 1.5p} -padding 0.75p
	    ttk::style map $style -indicatorbackground \
		[list {alternate disabled}	$colors(-disabledindicator) \
		      {alternate pressed}	$colors(-pressedindicator) \
		      alternate			$colors(-indicator) \
		      {selected disabled}	$colors(-disabledindicator) \
		      {selected pressed}	$colors(-pressedindicator) \
		      selected			$colors(-indicator) \
		      disabled			$colors(-frame) \
		      pressed			$colors(-darker)]
	}

	# N.B.: The values of the -arrowsize option for the styles
	# TMenubutton, TCombobox, TSpinbox, and TScrollbar must be specified
	# in pixels, because they are used in SVG images for chevrons.

	ttk::style configure TMenubutton \
	    -relief raised -arrowsize 5 -arrowpadding 2.25p \
	    -arrowcolor $colors(-text) -padding {7.5p 2.25p}
	ttk::style map TMenubutton \
	    -arrowcolor [list disabled $colors(-disabledfg)]

	ttk::style configure TEntry -insertwidth 0.75p \
	    -fieldbackground $colors(-window) -padding 0.75p \
	    -focuswidth 1.5p -focuscolor $colors(-selectbg)
	ttk::style map TEntry -fieldbackground \
	    [list readonly $colors(-frame) disabled $colors(-frame)]

	ttk::style configure TCombobox -insertwidth 0.75p \
	    -arrowpadding 2.25p -arrowsize 4 -arrowcolor $colors(-text) \
	    -fieldbackground $colors(-window) -padding 0.75p \
	    -focuswidth 0.75p -focuscolor $colors(-selectbg)
	ttk::style map TCombobox -fieldbackground \
	    [list readonly $colors(-frame) disabled $colors(-frame)] \
	    -arrowcolor [list disabled $colors(-disabledfg)]

	ttk::style configure TSpinbox -insertwidth 0.75p \
	    -arrowpadding 2.25p -arrowsize 3 -arrowcolor $colors(-text) \
	    -fieldbackground $colors(-window) -padding {1.5p 0.75p 7.5p 0.75p} \
	    -focuswidth 0.75p -focuscolor $colors(-selectbg)
	ttk::style map TSpinbox -fieldbackground \
	    [list readonly $colors(-frame) disabled $colors(-frame)] \
	    -arrowcolor [list disabled $colors(-disabledfg)]

	ttk::style configure TLabelframe \
	    -relief groove -borderwidth 2

	ttk::style configure TScrollbar \
	    -width 6.75p -arrowpadding 1.5p -arrowsize 4 \
	    -arrowcolor $colors(-text)
	ttk::style map TScrollbar \
	    -arrowcolor [list disabled $colors(-disabledfg)]

	ttk::style configure TScale \
	    -innercolor $colors(-selectbg) \
	    -outercolor $colors(-window) \
	    -bordercolor $colors(-darker) \
	    -groovewidth 3p
	ttk::style map TScale \
	    -outercolor [list active $colors(-activebg)]

	ttk::style configure TProgressbar \
	    -background $colors(-selectbg) \
	    -borderwidth 0 \
	    -barsize 22.5p \
	    -thickness 3p

	ttk::style configure TNotebook.Tab \
	    -padding {3p 1.5p} -background $colors(-darker)
	ttk::style map TNotebook.Tab \
	    -background [list selected $colors(-frame)] \
	    -highlight [list selected 1] \
	    -highlightcolor [list selected $colors(-selectbg)]

	# N.B.: The values of the -indicatorsize option for the
	# treeview-related styles Heading and Item must be specified
	# in pixels, because they are used in SVG images for chevrons.

	# Treeview
	ttk::style configure Heading \
	    -font TkHeadingFont -relief raised -padding 2.25p \
	    -indicatorsize 4 -indicatormargin {3p 1.5p 1.5p 1.5p}
	ttk::style configure Row -focuscolor black \
	    -focussolid 1 -focusthickness 0 -padding 0
	ttk::style map Row -focusthickness [list focus 1]
	ttk::style configure Item -indicatorsize 4 \
	    -indicatormargin {1.5p 1.5p 3p 1.5p}
	ttk::style configure CheckTreeview.Item \
	    -indicatormargin {0 0.75p 3p 0.75p}	;# for Checkbutton.indicator
	ttk::style map CheckTreeview.Item -indicatorbackground \
	    [list {selected disabled}	$colors(-disabledindicator) \
		  selected		$colors(-indicator) \
		  disabled		$colors(-frame)]
	ttk::style configure Treeview.Separator \
	    -background $colors(-alternate)
	ttk::style configure Treeview \
	    -background $colors(-window) \
	    -stripedbackground $colors(-alternate) \
	    -fieldbackground $colors(-window) \
	    -foreground $colors(-text) \
	    -indent 15p \
	    -focuswidth 0.75p -focuscolor $colors(-selectbg)
	ttk::style map Treeview \
	    -background [list	disabled $colors(-frame) \
				background $colors(-inactiveselectbg) \
				selected $colors(-selectbg) \
				active   $colors(-activebg)] \
	    -foreground [list	disabled $colors(-disabledfg) \
				background $colors(-inactiveselectfg) \
				selected $colors(-selectfg) \
				active   $colors(-foreground)]

	# Combobox popdown frame
	ttk::style layout ComboboxPopdownFrame {
	    ComboboxPopdownFrame.border -sticky nswe
	}
	ttk::style configure ComboboxPopdownFrame \
	    -borderwidth 1 -relief solid

	#
	# Toolbar buttons
	#
	ttk::style layout Toolbutton {
	    Toolbutton.border -children {
		Toolbutton.focus -children {
		    Toolbutton.padding -children {
			Toolbutton.label
		    }
		}
	    }
	}
	ttk::style configure Toolbutton \
	    -anchor center -justify center -padding 1.5p -relief flat \
	    -shiftrelief 1
	ttk::style map Toolbutton -relief \
	    [list disabled flat selected sunken pressed sunken active raised]
	ttk::style map Toolbutton -background \
	    [list pressed $colors(-darker)  active $colors(-activebg)]
    }
}

ttk::theme::default::reconfigureDefaultTheme

# ttk::theme::default::configureNotebookStyle --
#
# Sets theme-specific option values for the ttk::notebook tab style $style.Tab.
# Invoked by ::ttk::configureNotebookStyle.

proc ttk::theme::default::configureNotebookStyle {style} {
    set tabPos [ttk::style lookup $style -tabposition {} nw]
    switch -- [string index $tabPos 0] {
	n - s {
	    ttk::style configure $style.Tab -padding {3p 1.5p}
	}
	w - e {
	    ttk::style configure $style.Tab -padding {1.5p 3p}
	}
	default {
	    ttk::style configure $style.Tab -padding {3p 1.5p}
	}
    }
}
