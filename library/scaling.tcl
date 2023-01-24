# scaling.tcl --
#
# Contains scaling-related utility procedures.
#
# Copyright © 2022 Csaba Nemethi <csaba.nemethi@t-online.de>

# ::tk::ScalingPct --
#
# Returns the display's current scaling percentage (100, 125, 150, 175, 200, or
# a greater integer value).

namespace eval ::tk {
    namespace export ScalingPct ScaleNum
}

proc ::tk::ScalingPct {} {
    variable scalingPct
    if {[info exists scalingPct]} {
	return $scalingPct
    }

    set pct [expr {[tk scaling] * 75}]
    set origPct $pct

    set onX11 [expr {[tk windowingsystem] eq "x11"}]
    set usingSDL [expr {[info exists ::tk::sdltk] && $::tk::sdltk}]

    if {$onX11 && !$usingSDL} {
	#
	# Try to get the window scaling factor (1 or 2), partly
	# based on https://wiki.archlinux.org/title/HiDPI
	#
	set winScalingFactor 1
	variable fontScalingFactor 1		;# needed in the file ttk/fonts
	if {[catch {exec ps -e | grep xfce4-session}] == 0} {		;# Xfce
	    if {[catch {exec xfconf-query -c xsettings \
		 -p /Gdk/WindowScalingFactor} result] == 0} {
		set winScalingFactor $result
		if {$winScalingFactor >= 2} {
		    set fontScalingFactor 2
		}
	    }

	    #
	    # The DPI value can be set in the "Fonts" tab of the "Appearance"
	    # dialog or (on Linux Lite 5+) via the "HiDPI Settings" dialog.
	    #
	} elseif {[catch {exec ps -e | grep mate-session}] == 0} {	;# MATE
	    if {[catch {exec gsettings get org.mate.interface \
		 window-scaling-factor} result] == 0} {
		if {$result == 0} {			;# means: "Auto-detect"
		    #
		    # Try to get winScalingFactor from the cursor size
		    #
		    if {[catch {exec xrdb -query | grep Xcursor.size} result]
			== 0 &&
			[catch {exec gsettings get org.mate.peripherals-mouse \
			 cursor-size} defCursorSize] == 0} {
			set cursorSize [lindex $result 1]
			set winScalingFactor \
			    [expr {($cursorSize + $defCursorSize - 1) /
				   $defCursorSize}]
		    }
		} else {
		    set winScalingFactor $result
		}
	    }

	    #
	    # The DPI value can be set via the "Font Rendering Details"
	    # dialog, which can be opened using the "Details..." button
	    # in the "Fonts" tab of the "Appearance Preferences" dialog.
	    #
	} elseif {[catch {exec ps -e | grep gnome-session}] == 0 &&
		  [catch {exec gsettings get \
		   org.gnome.settings-daemon.plugins.xsettings overrides} \
		   result] == 0 &&
		  [set idx \
		   [string first "'Gdk/WindowScalingFactor'" $result]] >= 0} {
	    scan [string range $result $idx end] "%*s <%d>" winScalingFactor
	}

	#
	# Get the scaling percentage
	#
	if {$winScalingFactor >= 2} {
	    set pct 200
	} elseif {[catch {exec xrdb -query | grep Xft.dpi} result] == 0} {
	    #
	    # Derive the value of pct from that of the font DPI
	    #
	    set dpi [lindex $result 1]
	    set pct [expr {100 * $dpi / 96}]
	} elseif {[catch {exec ps -e | grep gnome-session}] == 0 &&
		  ![info exists ::env(WAYLAND_DISPLAY)] &&
		  [catch {exec xrandr | grep " connected"} result] == 0 &&
		  [catch {open $::env(HOME)/.config/monitors.xml} chan] == 0} {
	    #
	    # Update pct by scanning the file ~/.config/monitors.xml
	    #
	    ScanMonitorsFile $result $chan pct
	}
    }

    if {$pct < 100 + 12.5} {
	set pct 100
    } elseif {$pct < 125 + 12.5} {
	set pct 125
    } elseif {$pct < 150 + 12.5} {
	set pct 150
    } elseif {$pct < 175 + 12.5} {
	set pct 175
    } elseif {$pct < 200 + 12.5} {
	set pct 200
    } elseif {$pct < 225 + 12.5} {
	set pct 225
    } elseif {$pct < 250 + 12.5} {
	set pct 250
    } elseif {$pct < 275 + 12.5} {
	set pct 275
    } elseif {$pct < 300 + 25} {
	set pct 300
    } elseif {$pct < 350 + 25} {
	set pct 350
    } elseif {$pct < 400 + 25} {
	set pct 400
    } elseif {$pct < 450 + 25} {
	set pct 450
    } elseif {$pct < 500 + 25} {
	set pct 500
    } else {
	set pct [expr {int($pct + 0.5)}]
    }

    if {$onX11 && $pct != 100 && $pct != $origPct} {
	#
	# Set Tk's scaling factor according to $pct
	#
	tk scaling [expr {$pct / 75.0}]
    }

    #
    # Set the variable scalingPct to $pct and make it read-only
    #
    set scalingPct $pct
    trace add variable scalingPct {write unset} \
	[list ::tk::RestoreScalingPct $scalingPct]

    return $pct
}

# ::tk::ScaleNum --
#
# Scales a nonnegative integer according to the display's current scaling
# percentage.
#
# Arguments:
#   num - A nonnegative integer.

proc ::tk::ScaleNum num {
    set pct [::tk::ScalingPct]
    set factor [expr {$num * $pct}]
    set result [expr {$factor / 100}]
    if {$factor % 100 >= 50} {
	incr result
    }

    return $result
}

# ::tk::ScanMonitorsFile --
#
# Updates the scaling percentage by scanning the file ~/.config/monitors.xml.
#
# Arguments:
#   xrandrResult - The output of 'xrandr | grep " connected"'.
#   chan -	   Returned from 'open ~/.config/monitors.xml'.
#   pctName -	   The name of a variable containing the scaling percentage.

proc ::tk::ScanMonitorsFile {xrandrResult chan pctName} {
    upvar $pctName pct

    #
    # Get the list of connected outputs reported by xrandr
    #
    set outputList {}
    foreach line [split $xrandrResult "\n"] {
	set idx [string first " " $line]
	set output [string range $line 0 [incr idx -1]]
	lappend outputList $output
    }
    set outputList [lsort $outputList]

    #
    # Get the content of the file ~/.config/monitors.xml
    #
    set str [read $chan]
    close $chan

    #
    # Run over the file's "configuration" sections
    #
    set idx 0
    while {[set idx2 [string first "<configuration>" $str $idx]] >= 0} {
	set idx2 [string first ">" $str $idx2]
	set idx [string first "</configuration>" $str $idx2]
	set config [string range $str [incr idx2] [incr idx -1]]

	#
	# Get the list of connectors within this configuration
	#
	set connectorList {}
	foreach {dummy connector} [regexp -all -inline \
		{<connector>([^<]+)</connector>} $config] {
	    lappend connectorList $connector
	}
	set connectorList [lsort $connectorList]

	#
	# If $outputList and $connectorList are identical then set the
	# variable pct to 100 or 200, depending on the max. scaling
	# within this configuration, and exit the loop.  (Due to the
	# way fractional scaling is implemented in GNOME, we have to
	# set the variable pct to 200 rather than 125, 150, or 175.)
	#
	if {[string compare $outputList $connectorList] == 0} {
	    set maxScaling 0.0
	    foreach {dummy scaling} [regexp -all -inline \
		    {<scale>([^<]+)</scale>} $config] {
		if {$scaling > $maxScaling} {
		    set maxScaling $scaling
		}
	    }
	    set pct [expr {$maxScaling > 1.0 ? 200 : 100}]
	    break
	}
    }
}

# ::tk::RestoreScalingPct --
#
# This trace procedure is executed whenever the variable scalingPct is written
# or unset.  It restores the variable to its original value, given by the first
# argument.
#
# Arguments:
#   origVal - The original value of the variable scalingPct.
#   varName - The name of the variable ("(::)tk::scalingPct" or "scalingPct").
#   index   - An empty string.
#   op -      One of "write" or "unset".

proc ::tk::RestoreScalingPct {origVal varName index op} {
    variable scalingPct $origVal
    switch $op {
	write { return -code error "the variable is read-only" }
	unset {
	    trace add variable scalingPct {write unset} \
		[list ::tk::RestoreScalingPct $scalingPct]
	}
    }
}
