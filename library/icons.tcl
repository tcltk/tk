# icons.tcl --
#
#	A set of stock icons for use in Tk dialogs. The icons used here
#	were provided by the Vimix Icon Theme project, which provides a
#	unified set of high quality icons licensed under the
#	Creative Commons Attribution Share-Alike license
#	(https://creativecommons.org/licenses/by-sa/4.0/)
#
#	See https://github.com/vinceliuice/vimix-icon-theme
#
# Copyright © 2009 Pat Thoyts <patthoyts@users.sourceforge.net>
# Copyright © 2022 Harald Oehlmann <harald.oehlmann@elmicron.de>
# Copyright © 2022 Csaba Nemethi <csaba.nemethi@t-online.de>

namespace eval ::tk::icons {}

variable ::tk::svgFmt [::tk_svgFormat]

image create photo ::tk::icons::error -format $::tk::svgFmt -data {
    <?xml version="1.0" encoding="UTF-8"?>
    <svg width="32" height="32" version="1.1" xmlns="http://www.w3.org/2000/svg">
     <circle cx="16" cy="16" r="16" fill="#d32f2f"/>
     <g transform="rotate(45,16,16)" fill="#fff">
      <rect x="6" y="14" width="20" height="4"/>
      <rect x="14" y="6" width="4" height="20"/>
     </g>
    </svg>
}

image create photo ::tk::icons::warning -format $::tk::svgFmt -data {
    <?xml version="1.0" encoding="UTF-8"?>
    <svg width="32" height="32" version="1.1" xmlns="http://www.w3.org/2000/svg">
     <circle cx="16" cy="16" r="16" fill="#f67400"/>
     <circle cx="16" cy="24" r="2" fill="#fff"/>
     <path d="m14 20h4v-14h-4z" fill="#fff"/>
    </svg>
}

image create photo ::tk::icons::information -format $::tk::svgFmt -data {
    <?xml version="1.0" encoding="UTF-8"?>
    <svg width="32" height="32" version="1.1" xmlns="http://www.w3.org/2000/svg">
     <circle cx="16" cy="16" r="16" fill="#2091df"/>
     <circle cx="16" cy="8" r="2" fill="#fff"/>
     <path d="m14 12h4v14h-4z" fill="#fff"/>
    </svg>
}

image create photo ::tk::icons::question -format $::tk::svgFmt -data {
    <?xml version="1.0" encoding="UTF-8"?>
    <svg width="32" height="32" version="1.1" xmlns="http://www.w3.org/2000/svg">
     <circle cx="16" cy="16" r="16" fill="#5c6bc0"/>
     <path d="m17.6 27.2h-3.2v-3.2h3.2zm3.312-12.4-1.44 1.472c-1.152 1.168-1.872 2.128-1.872 4.528h-3.2v-0.8c0-1.76 0.72-3.36 1.872-4.528l1.984-2.016a3.128 3.128 0 0 0 0.944-2.256c0-1.76-1.44-3.2-3.2-3.2s-3.2 1.44-3.2 3.2h-3.2c0-3.536 2.864-6.4 6.4-6.4s6.4 2.864 6.4 6.4c0 1.408-0.576 2.688-1.488 3.6z" fill="#fff"/>
    </svg>
}

# ::tk::ImageSvgCopy
#
# Image copy is a raster copy, not a full SVG copy.  Ticket [7eb3455c4a].
# Use this command in place of [$to copy $from].
#
# Arguments:
#
# from    - name of image to copy from
# to      - name of image to copy to
#
proc ::tk::ImageSvgCopy {from to} {
    foreach {line} [$from configure] {
	set opt [lindex $line 0]
	$to configure $opt [$from cget $opt]
    }
    return
}

# ::tk::PrepareIconsForDisplay
#
# Arguments:
#
# w    - a Tk window path
#
# When there are multiple displays, they may differ in dpi and [tk scaling].
# Different sized images are then needed for each display.  Also the script
# may have changed the [tk scaling] since the icons were last used.
# This command checks that dialog icons for the -displayof $w exist, and
# creates and/or scales them if necessary.
#
# Return value: the per-display suffix used for icon names on this screen.

proc ::tk::PrepareIconsForDisplay {w} {
    set screen [winfo screen $w]

    # Ensure that icons exist.
    if {$screen eq [winfo screen .]} {
	# The "main" display is that of the root window "." and is not
	# necessarily ":0.0".  Icons for this display already exist.
	set screen {}
	set w2 .
    } elseif {abs([tk scaling -displayof $w] - [tk scaling]) < 0.01} {
	# Ignore small differences. Re-use the icons for the root window's
	# display if they are close enough in size (1%).
	set screen {}
	set w2 .
    } else {
	# Ensure that namespace separators never occur in the screen name (as
	# they cause problems in variable names). Double-colons exist in some
	# VNC screen names. [Bug 2912473]
	set screen [string map {:: _doublecolon_} $screen]

	foreach img {error information question warning} {
	    set imgName ::tk::icons::${img}${screen}
	    if {"$imgName" ni [image names]} {
		image create photo $imgName
	       #$imgName copy ::tk::icons::${img} is only a raster copy
		ImageSvgCopy ::tk::icons::${img} $imgName
	    }
	}
	set w2 $w
    }

    # Now scale the chosen icons if necessary.
    set oldScaling [lindex [::tk::icons::error$screen cget -format] 2]
    set newScaling [lindex [::tk_svgFormat -displayof $w2] 2]
    if {[expr {abs($oldScaling - $newScaling)}] > 0.01} {
	::tk::RescaleSvgImages $w2
    }
    return $screen
}

# ::tk::RescaleSvgImages
#
# Arguments:
#
# w    - a Tk window path
#
# Set the scaling of the system SVG icons for -displayof $w, if they have been
# defined, to the current value of "tk scaling" for that display.
#
# Return value: none.

proc ::tk::RescaleSvgImages {w} {
    if {![winfo exists $w]} {
	return -code error "window \"$w\" does not exist"
    }

    set screen [winfo screen $w]
    if {$screen eq [winfo screen .]} {
	set screen {}
    }

    # Ensure that namespace separators never occur in the screen name (as
    # they cause problems in variable names). Double-colons exist in some
    # VNC screen names. [Bug 2912473]
    set screen [string map {:: _doublecolon_} $screen]

    set svgFormat [::tk_svgFormat -displayof $w]
    foreach img {
	::tk::icons::error
	::tk::icons::warning
	::tk::icons::information
	::tk::icons::question
    } {
	if {"$img$screen" in [image names]} {
	    $img$screen configure -format $svgFormat
	}
    }
    return
}
