# palette.tcl --
#
# This file contains procedures that change the color palette used
# by Tk.
#
# Copyright © 1995-1997 Sun Microsystems, Inc.
#
# See the file "license.terms" for information on usage and redistribution
# of this file, and for a DISCLAIMER OF ALL WARRANTIES.
#

# ::tk_setPalette --
# Changes the default color scheme for a Tk application by setting
# default colors in the option database and by modifying all of the
# color options for existing widgets that have the default value.
#
# Arguments:
# The arguments consist of either a single color name, which
# will be used as the new background color (all other colors will
# be computed from this) or an even number of values consisting of
# option names and values.  The name for an option is the one used
# for the option database, such as activeForeground, not -activeforeground.

proc ::tk_setPalette {args} {
    if {[winfo depth .] == 1} {
	# Just return on monochrome displays, otherwise errors will occur
	return
    }

    # Create an array that has the complete new palette.  If some colors
    # aren't specified, compute them from other colors that are specified.

    if {[llength $args] == 1} {
	set new(background) [lindex $args 0]
    } else {
	array set new $args
    }
    if {![info exists new(background)]} {
	return -code error -errorcode {TK SET_PALETTE BACKGROUND} \
	    "must specify a background color"
    }
    set bg [winfo rgb . $new(background)]
    if {![info exists new(foreground)]} {
	set new(foreground) [::tk::ContrastColor $new(background)]
    }
    set darkerBg [::tk::Darken $new(background) 90]

    foreach i {activeForeground insertBackground selectForeground \
	    highlightColor} {
	if {![info exists new($i)]} {
	    set new($i) $new(foreground)
	}
    }
    if {![info exists new(disabledForeground)]} {
	set new(disabledForeground) \
		[::tk::BlendColors $new(background) $new(foreground)]
    }
    if {![info exists new(highlightBackground)]} {
	set new(highlightBackground) $new(background)
    }
    # 'buttonBackground' is the background color of the buttons in
    # the spinbox widget.
    if {![info exists new(buttonBackground)]} {
	set new(buttonBackground) $new(background)
    }
    # 'selectColor' is the background of check & radio buttons.
    if {![info exists new(selectColor)]} {
	foreach {r g b} $bg {break}
	if {$r+1.5*$g+0.5*$b > 100000} {
	    set new(selectColor) white
	} else {
	    set new(selectColor) black
	}
    }
    if {![info exists new(activeBackground)]} {
	set new(activeBackground) [::tk::Lighten $new(background)]
    }
    if {![info exists new(selectBackground)]} {
	set new(selectBackground) $darkerBg
    }
    if {![info exists new(troughColor)]} {
	set new(troughColor) $darkerBg
    }

    # let's make one of each of the widgets so we know what the
    # defaults are currently for this platform.
    toplevel .___tk_set_palette
    wm withdraw .___tk_set_palette
    foreach q {
	button canvas checkbutton entry frame label labelframe
	listbox menubutton menu message radiobutton scale scrollbar
	spinbox text
    } {
	$q .___tk_set_palette.$q
    }

    # Walk the widget hierarchy, recoloring all existing windows.
    # The option database must be set according to what we do here,
    # but it breaks things if we set things in the database while
    # we are changing colors...so, ::tk::RecolorTree now returns the
    # option database changes that need to be made, and they
    # need to be evalled here to take effect.
    # We have to walk the whole widget tree instead of just
    # relying on the widgets we've created above to do the work
    # because different extensions may provide other kinds
    # of widgets that we don't currently know about, so we'll
    # walk the whole hierarchy just in case.

    eval [tk::RecolorTree . new]

    destroy .___tk_set_palette

    # Change the option database so that future windows will get the
    # same colors.

    foreach option [array names new] {
	option add *$option $new($option) widgetDefault
    }

    # Save the options in the variable ::tk::Palette, for use the
    # next time we change the options.

    array set ::tk::Palette [array get new]

    if {[tk windowingsystem] ne "x11" || [ttk::style theme use] ne "default"} {
	return
    }

    # Update the 'default' ttk theme with the new palette,
    # and then set 'default' as the current ttk theme,
    # in order to apply the new palette to the ttk widgets.

    foreach option [array names new] {
	if {[info exists ttk::theme::default::colorOptionLookup($option)]} {
	    foreach colorName $ttk::theme::default::colorOptionLookup($option) {
		set ttk::theme::default::colors($colorName) $new($option)
	    }
	}
    }
    ttk::theme::default::reconfigureDefaultTheme
    ttk::setTheme default

    return
}

# ::tk::RecolorTree --
# This procedure changes the colors in a window and all of its
# descendants, according to information provided by the colors
# argument. This looks at the defaults provided by the option
# database, if it exists, and if not, then it looks at the default
# value of the widget itself.
#
# Arguments:
# w -			The name of a window.  This window and all its
#			descendants are recolored.
# colors -		The name of an array variable in the caller,
#			which contains color information.  Each element
#			is named after a widget configuration option, and
#			each value is the value for that option.
# Return Value:
#                       A list of commands which can be run to update
#                       the defaults database when exec'ed.

proc ::tk::RecolorTree {w colors} {
    upvar $colors c
    set result {}
    set prototype .___tk_set_palette.[string tolower [winfo class $w]]
    if {![winfo exists $prototype]} {
	unset prototype
    }
    foreach dbOption [array names c] {
	set option -[string tolower $dbOption]
	set class [string replace $dbOption 0 0 [string toupper \
	     [string index $dbOption 0]]]
	# Make sure this option is valid for this window.
	if {![catch {$w configure $option} value]} {
	    # Retrieve a default value for this option.  First check
	    # the option database. If it is not in the database use
	    # the value for the temporary prototype widget.
	    set defaultcolor [option get $w $dbOption $class]
	    if {$defaultcolor eq "" || \
		    ([info exists prototype] && \
		    [$prototype cget $option] ne "$defaultcolor")} {
		set defaultcolor [lindex $value 3]
	    }
	    if {$defaultcolor ne ""} {
		set defaultcolor [winfo rgb . $defaultcolor]
	    }
	    # The colour the window has now, which is not the default one
	    # when the application has given the window a colour of its own.
	    set currentcolor [lindex $value 4]
	    if {$currentcolor ne ""} {
		set currentcolor [winfo rgb . $currentcolor]
	    }
	    if {[string match $defaultcolor $currentcolor]} {
		# The window still has the colour it was given by default, so
		# give it the one the new palette has for it, and change the
		# option database so that the windows made later get it too.
		append result ";\noption add [list \
		    *[winfo class $w].$dbOption $c($dbOption) 60]"
		$w configure $option $c($dbOption)
	    }
	}
    }
    foreach child [winfo children $w] {
	append result ";\n[::tk::RecolorTree $child c]"
    }
    return $result
}

# ::tk::ContrastColor --
# Given a color name, returns black or white, whichever is the more legible
# against it.  Note that the range of each value in the triple returned by
# [winfo rgb] is 0-65535, and that your eyes are more sensitive to green
# than to red, and more to red than to blue.
#
# Arguments:
# color -	Name of the color to be contrasted with.

proc ::tk::ContrastColor {color} {
    lassign [winfo rgb . $color] r g b
    if {$r + 1.5*$g + 0.5*$b > 100000} {
	return black
    } else {
	return white
    }
}

# ::tk::Lighten --
# Given a color name, computes the lighter color which tk_setPalette uses
# for an active background: each component is rounded up by 15%, or by one
# third of the way to full white, whichever is the greater.
#
# Arguments:
# color -	Name of starting color.

proc ::tk::Lighten {color} {
    set result {}
    foreach component [winfo rgb . $color] {
	set value [expr {$component/256}]
	set inc1 [expr {($value*15)/100}]
	set inc2 [expr {(255-$value)/3}]
	incr value [expr {$inc1 > $inc2 ? $inc1 : $inc2}]
	if {$value > 255} {
	    set value 255
	}
	lappend result $value
    }
    return [format #%02x%02x%02x {*}$result]
}

# ::tk::BlendColors --
# Given two color names, computes their weighted average.  The default
# weights are the ones tk_setPalette uses to compute a disabled foreground
# from a background and a foreground: three parts to one.
#
# Arguments:
# color1 -	Name of the first color.
# color2 -	Name of the second color.
# weight1 -	Weight of the first color.
# weight2 -	Weight of the second color.

proc ::tk::BlendColors {color1 color2 {weight1 3} {weight2 1}} {
    set total [expr {($weight1 + $weight2)*256}]
    set result {}
    foreach c1 [winfo rgb . $color1] c2 [winfo rgb . $color2] {
	lappend result [expr {($weight1*$c1 + $weight2*$c2)/$total}]
    }
    return [format #%02x%02x%02x {*}$result]
}

# ::tk::Darken --
# Given a color name, computes a new color value that darkens (or
# brightens) the given color by a given percent.
#
# Arguments:
# color -	Name of starting color.
# percent -	Integer telling how much to brighten or darken as a
#		percent: 50 means darken by 50%, 110 means brighten
#		by 10%.

proc ::tk::Darken {color percent} {
    if {$percent < 0} {
	return #000000
    } elseif {$percent > 200} {
	return #ffffff
    } elseif {$percent <= 100} {
	# The scaling is done on the component as [winfo rgb] gives it, and
	# brought down to eight bits once, rather than being brought down
	# first and scaled afterwards: dividing twice loses up to a whole
	# unit of the result.
	lassign [winfo rgb . $color] r g b
	set r [expr {($r*$percent)/25600}]
	set g [expr {($g*$percent)/25600}]
	set b [expr {($b*$percent)/25600}]
    } elseif {$percent > 100} {
	lassign [winfo rgb . $color] r g b
	set r [expr {255 - ((65535-$r)*(200-$percent))/25600}]
	set g [expr {255 - ((65535-$g)*(200-$percent))/25600}]
	set b [expr {255 - ((65535-$b)*(200-$percent))/25600}]
    }
    return [format #%02x%02x%02x $r $g $b]
}

# ::tk_bisque --
# Reset the Tk color palette to the old "bisque" colors.
#
# Arguments:
# None.

proc ::tk_bisque {} {
    tk_setPalette activeBackground #e6ceb1 activeForeground black \
	    background #ffe4c4 disabledForeground #b0b0b0 foreground black \
	    highlightBackground #ffe4c4 highlightColor black \
	    insertBackground black \
	    selectBackground #e6ceb1 selectForeground black \
	    troughColor #cdb79e
}

# ::tk::CapturePalette --
# Returns the palette in effect, in the form tk_setPalette accepts: the
# colour each palette option now defaults to.  The values are read from one
# widget of each kind, as tk_setPalette reads them, so that a colour Tk has
# compiled in is reported as exactly that and not as something derived from
# the background.
#
# Arguments:
# None.

proc ::tk::CapturePalette {} {
    set probe [toplevel .___tk_capture_palette]
    wm withdraw $probe
    set widgets {}
    foreach class {
	frame label button entry listbox text checkbutton scale scrollbar
	spinbox menu
    } {
	lappend widgets [$class $probe.$class]
    }

    set palette {}
    foreach option {
	activeBackground activeForeground background buttonBackground
	disabledForeground foreground highlightBackground highlightColor
	insertBackground selectBackground selectColor selectForeground
	troughColor
    } {
	foreach w $widgets {
	    if {![catch {$w cget -[string tolower $option]} color]
		    && $color ne ""} {
		lappend palette $option $color
		break
	    }
	}
    }

    destroy $probe
    return $palette
}

# ::tk::DatabaseColors --
# The palette colours the option database answers for, in the form
# tk_setPalette accepts.  Read before Tk has written a palette of its own,
# these are the colours the user asked for in X resources, which outrank
# anything the desktop asks for through the portal.
#
# Arguments:
# None.

proc ::tk::DatabaseColors {} {
    set colors {}
    foreach {option class} {
	activeBackground	ActiveBackground
	background		Background
	disabledForeground	DisabledForeground
	foreground		Foreground
	selectBackground	SelectBackground
	selectForeground	SelectForeground
	troughColor		TroughColor
    } {
	set color [option get . $option $class]
	if {$color ne ""} {
	    dict set colors $option $color
	}
    }
    return $colors
}

# ::tk::SetAppearance --
# Applies the palette which [wm attributes $window -appearance] asks for.
# On X11 a window has no appearance of its own, so this is the palette of
# the whole application: the option database which tk_setPalette writes to
# has no per-window scope.  Called from the C implementation of the
# attribute, which has already checked the value.
#
# Only the colours which go with the background change.  The selection
# colour does not: it is an accent, which goes with a light background and a
# dark one alike, and the classic widgets and the themed ones have each had
# their own.  See ticket [807b2f8916].
#
# Arguments:
# appearance -	light, dark or auto.  "light" and "dark" ask for a palette
#		whatever the user's X resources say; "auto" goes back to the
#		palette which was in effect before the attribute was first
#		set, which is the one Tk started with.

proc ::tk::SetAppearance {appearance} {
    variable SavedPalette
    variable SavedTtkColors
    variable SavedDatabaseColors

    # What the desktop asks for, which only "auto" follows.
    set desktop {colorScheme {} accentColor {}}
    if {$appearance eq "auto" && [tk windowingsystem] eq "x11"} {
	set desktop [::tk::portal::Appearance]
    }
    set scheme [dict get $desktop colorScheme]

    if {$appearance eq "auto" && $scheme eq ""
	    && ![info exists SavedPalette]} {
	# The desktop asks for nothing and nothing has been applied, so there
	# is nothing to go back to.
	set ::tk::appearance $appearance
	return
    }
    if {![info exists SavedPalette]} {
	set SavedPalette [CapturePalette]
	set SavedTtkColors [array get ::ttk::theme::default::colors]
	set SavedDatabaseColors [DatabaseColors]
    }

    # The themed widgets are kept in step only where tk_setPalette keeps
    # them: the default theme on X11.
    set themed [expr {[tk windowingsystem] eq "x11"
	    && [ttk::style theme use] eq "default"}]

    if {$appearance eq "auto" && $scheme eq ""} {
	tk_setPalette {*}$SavedPalette
	array set ::ttk::theme::default::colors $SavedTtkColors
    } else {
	if {$appearance ne "auto"} {
	    set scheme $appearance
	}

	# The light background is the one Tk has always had; the rest is
	# computed from it, except the selection colours.  An accent goes
	# with a light frame and a dark one alike, so the palette keeps
	# its own unless the desktop has one to offer.
	set palette [dict create background \
		[dict get {light #d9d9d9 dark #3c3c3c} $scheme] \
		selectBackground [dict get $SavedPalette selectBackground] \
		selectForeground [dict get $SavedPalette selectForeground]]
	set accent [dict filter $SavedTtkColors \
		key -selectbg -selectfg -indicator -pressedindicator]
	set accentColor [dict get $desktop accentColor]
	if {$accentColor ne ""} {
	    set accentForeground [ContrastColor $accentColor]
	    dict set palette selectBackground $accentColor
	    dict set palette selectForeground $accentForeground
	    set accent [dict create -selectbg $accentColor \
		    -selectfg $accentForeground -indicator $accentColor \
		    -pressedindicator [Lighten $accentColor]]
	}

	# A colour the user chose in X resources outranks the desktop's.
	if {$appearance eq "auto"} {
	    set palette [dict merge $palette $SavedDatabaseColors]
	}
	tk_setPalette {*}$palette
	if {$themed} {
	    # tk_setPalette reaches three of the theme's thirteen colours, so
	    # compute the rest from the ones the option database now holds,
	    # imposing the accent over the shade of the background which
	    # tk_setPalette has just written there.
	    ::ttk::theme::default::seedColors $accent
	}
    }
    if {$themed} {
	::ttk::theme::default::reconfigureDefaultTheme
	::ttk::setTheme default
    }

    set ::tk::appearance $appearance
    return
}

# ::tk::Appearance --
# Implements [tk appearance], which reports the appearance the desktop asks
# applications to use and changes nothing.  On X11 the desktop is asked
# through the XDG desktop portal; see TIP 767.
#
# Arguments:
# setting -	One of the settings, or nothing for all of them.

proc ::tk::Appearance {{setting ""}} {
    if {[tk windowingsystem] eq "x11"} {
	set appearance [::tk::portal::Appearance]
    } else {
	# The systems which give a window an appearance of their own do not
	# answer for these settings yet.
	set appearance {}
    }
    if {$setting eq ""} {
	return $appearance
    }
    if {![dict exists $appearance $setting]} {
	return -code error -errorcode [list TK LOOKUP SETTING $setting] \
		"bad setting \"$setting\": must be colorScheme, accentColor,\
		contrast, or reducedMotion"
    }
    return [dict get $appearance $setting]
}

# ::tk::DesktopAppearanceChanged --
# Called when the desktop has changed the appearance it asks for.  The
# palette follows where the application has left the appearance to the
# desktop, and the events of TIP 750 are sent whether it has or not, so that
# an application which colours itself can follow too.
#
# Arguments:
# None.

proc ::tk::DesktopAppearanceChanged {} {
    if {![info exists ::tk::appearance] || $::tk::appearance eq "auto"} {
	SetAppearance auto
    }

    # The appearance the application now has, which is what the other
    # platforms report in the data of <<AppearanceChanged>>.
    set appearance [expr {[winfo isdark .] ? "dark" : "light"}]
    foreach w [linsert [winfo children .] 0 .] {
	if {[winfo toplevel $w] ne $w} {
	    continue
	}
	event generate $w <<AppearanceChanged>> -data $appearance
	event generate $w <<[string totitle $appearance]Appearance>>
    }
    return
}
