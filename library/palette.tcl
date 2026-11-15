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

    # Remember what we are about to change, so that tk_restorePalette can
    # put it back.  The original state is captured once, by the first call;
    # later calls only add to it.

    if {![info exists ::tk::PaletteSaved]} {
	set ::tk::PaletteSaved [array get ::tk::Palette]
	if {[tk windowingsystem] eq "x11"
		&& [array exists ::ttk::theme::default::colors]} {
	    set ::tk::PaletteTtk [array get ::ttk::theme::default::colors]
	}
    }
    # This must happen before ::tk::RecolorTree runs, because that adds
    # class-specific entries to the option database that would hide the
    # original value of the wildcard entries.
    foreach option [array names new] {
	if {![info exists ::tk::PaletteDb($option)]} {
	    set ::tk::PaletteDb($option) \
		    [option get . $option [string totitle $option 0 0]]
	}
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
	    set rawDefault $defaultcolor
	    if {$defaultcolor ne ""} {
		set defaultcolor [winfo rgb . $defaultcolor]
	    }
	    # The colour the window has now, which is not the default one
	    # when the application has given the window a colour of its own.
	    set currentcolor [set rawCurrent [lindex $value 4]]
	    if {$currentcolor ne ""} {
		set currentcolor [winfo rgb . $currentcolor]
	    }
	    if {[string match $defaultcolor $currentcolor]} {
		# The window still has the colour it was given by default, so
		# give it the one the new palette has for it, and change the
		# option database so that the windows made later get it too.
		# Keep what is being replaced for tk_restorePalette: the
		# default for the class (for windows made later), and the
		# value of this particular window.

		set pattern *[winfo class $w].$dbOption
		if {$rawDefault ne ""
			&& ![info exists ::tk::PaletteClass($pattern)]} {
		    set ::tk::PaletteClass($pattern) $rawDefault
		}
		# A window that has the previous palette's color was made
		# after it, so it has no color of its own to go back to.
		set key [list $w $option]
		set own 1
		if {[info exists ::tk::Palette($dbOption)]
			&& ![catch {winfo rgb . $::tk::Palette($dbOption)} prev]
			&& $prev eq $currentcolor} {
		    set own 0
		}
		if {$own && ![string match .___tk_set_palette* $w]
			&& ![info exists ::tk::PaletteWidget($key)]} {
		    set ::tk::PaletteWidget($key) \
			    [list $rawCurrent [winfo class $w]]
		}
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

# ::tk_restorePalette --
# Undoes tk_setPalette (and tk_bisque): windows that still have a color
# the palette gave them get back the color they had before the first
# tk_setPalette call, and the option database is changed so that windows
# made later get the original defaults again.  Windows whose colors the
# application has set itself since are left alone.  It does nothing if
# tk_setPalette has not changed anything.
#
# Arguments:
# None.

proc ::tk_restorePalette {} {
    if {![info exists ::tk::PaletteSaved]} {
	return
    }

    # Existing windows first.  Nothing here touches the option database.

    ::tk::RestorePaletteTree .

    # Windows made from now on.  Entries are overwritten rather than
    # removed, because the option database has no way to remove one.

    foreach {pattern value} [array get ::tk::PaletteClass] {
	option add $pattern $value 60
    }
    foreach {option value} [array get ::tk::PaletteDb] {
	if {$value ne ""} {
	    option add *$option $value widgetDefault
	}
    }

    array unset ::tk::Palette
    array set ::tk::Palette $::tk::PaletteSaved

    if {[info exists ::tk::PaletteTtk]} {
	array set ::ttk::theme::default::colors $::tk::PaletteTtk
	ttk::theme::default::reconfigureDefaultTheme
	if {[ttk::style theme use] eq "default"} {
	    ttk::setTheme default
	}
    }

    # Forget everything: the next tk_setPalette starts a new history.

    array unset ::tk::PaletteDb
    array unset ::tk::PaletteClass
    array unset ::tk::PaletteWidget
    catch {unset ::tk::PaletteTtk}
    unset ::tk::PaletteSaved
    return
}

# ::tk::RestorePaletteTree --
# Restores the colors of a window and all of its descendants for
# tk_restorePalette.  A color is only changed if it is the one the current
# palette has for that option.  The value to go back to is the one recorded
# for that window if there is one, else the recorded default of its class.
#
# Arguments:
# w -		The name of a window.

proc ::tk::RestorePaletteTree {w} {
    variable Palette
    variable PaletteDb
    variable PaletteClass
    variable PaletteWidget

    set class [winfo class $w]
    foreach dbOption [array names PaletteDb] {
	if {![info exists Palette($dbOption)]} {
	    continue
	}
	set option -[string tolower $dbOption]
	if {[catch {$w configure $option} value]} {
	    continue
	}
	set current [lindex $value 4]
	if {$current eq "" || [catch {winfo rgb . $current} rgb1]
		|| [catch {winfo rgb . $Palette($dbOption)} rgb2]
		|| $rgb1 ne $rgb2} {
	    continue
	}
	set key [list $w $option]
	if {[info exists PaletteWidget($key)]
		&& [lindex $PaletteWidget($key) 1] eq $class} {
	    set original [lindex $PaletteWidget($key) 0]
	} elseif {[info exists PaletteClass(*$class.$dbOption)]} {
	    set original $PaletteClass(*$class.$dbOption)
	} else {
	    continue
	}
	$w configure $option $original
    }
    foreach child [winfo children $w] {
	RestorePaletteTree $child
    }
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
