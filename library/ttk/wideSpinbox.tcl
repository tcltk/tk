#==============================================================================
# wideSpinbox.tcl - Copyright © 2025 Csaba Nemethi <csaba.nemethi@t-online.de>
#
# Contains the implementation of the Wide.TSpinbox layout, and arranges for it
# to be created automatically for the current theme when needed.
#
# Usage:
#   ttk::spinbox <pathName> -style Wide.TSpinbox ...
#
# Structure of the module:
#   - Private procedures and data
#   - Public procedures
#==============================================================================

# Private procedures and data
# ===========================

interp alias {} ttk::wideSpinbox::CreateElem {} ttk::style element create

namespace eval ttk::wideSpinbox {
    variable uparrowImgData {
<svg width="20" height="16" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <circle cx="10" cy="8" r="8" fill="bg"/>
 <path d="m6 10 4-4 4 4" fill="none" stroke-linecap="round" stroke-linejoin="round" }
    variable downarrowImgData {
<svg width="20" height="16" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <circle cx="10" cy="8" r="8" fill="bg"/>
 <path d="m6 6 4 4 4-4" fill="none" stroke-linecap="round" stroke-linejoin="round" }

    variable onAndroid	  [expr {[info exists ::tk::android] && $::tk::android}]
    variable madeElements 0
}

#------------------------------------------------------------------------------
# ttk::wideSpinbox::NormalizeColor
#
# Returns the representation of a given color in the form "#RRGGBB".
#------------------------------------------------------------------------------
proc ttk::wideSpinbox::NormalizeColor color {
    lassign [winfo rgb . $color] r g b
    return [format "#%02x%02x%02x" \
	    [expr {$r >> 8}] [expr {$g >> 8}] [expr {$b >> 8}]]
}

#------------------------------------------------------------------------------
# ttk::wideSpinbox::CreateElements
#
# Creates the elements WideSpinbox.uparrow, WideSpinbox.downarrow, and
# WideSpinbox.gap of the Wide.TSpinbox layout for a given theme.
#------------------------------------------------------------------------------
proc ttk::wideSpinbox::CreateElements theme {
    # Create the WideSpinbox.uparrow element

    variable uparrowImgsArr
    set img  [CreateImg]
    set dImg [CreateImg]
    set pImg [CreateImg]
    set aImg [CreateImg]
    set uparrowImgsArr($theme) [list $img $dImg $pImg $aImg]
    CreateElem WideSpinbox.uparrow image [list $img \
	    disabled $dImg  pressed $pImg  active $aImg]

    # Create the WideSpinbox.downarrow element

    variable downarrowImgsArr
    set img  [CreateImg]
    set dImg [CreateImg]
    set pImg [CreateImg]
    set aImg [CreateImg]
    set downarrowImgsArr($theme) [list $img $dImg $pImg $aImg]
    CreateElem WideSpinbox.downarrow image [list $img \
	    disabled $dImg  pressed $pImg  active $aImg]

    # Create the WideSpinbox.gap element

    variable gapImg
    CreateElem WideSpinbox.gap image $gapImg

    # Create the Wide.TSpinbox layout

    if {$theme eq "classic"} {
	ttk::style layout Wide.TSpinbox {
	    Entry.highlight -sticky nswe -children {
		Entry.field -sticky nswe -children {
		    WideSpinbox.uparrow -side right -sticky e
		    WideSpinbox.gap -side right -sticky e
		    WideSpinbox.downarrow -side right -sticky e
		    Entry.padding -sticky nswe -children {
			Entry.textarea -sticky nsew
		    }
		}
	    }
	}
    } else {
	ttk::style layout Wide.TSpinbox {
	    Entry.field -sticky nswe -children {
		WideSpinbox.uparrow -side right -sticky e
		WideSpinbox.gap -side right -sticky e
		WideSpinbox.downarrow -side right -sticky e
		Entry.padding -sticky nswe -children {
		    Entry.textarea -sticky nsew
		}
	    }
	}
    }

    set padding [ttk::style lookup TEntry -padding]
    ttk::style configure Wide.TSpinbox -padding $padding
}

#------------------------------------------------------------------------------
# ttk::wideSpinbox::UpdateElements
#
# Updates the elements WideSpinbox.uparrow and WideSpinbox.downarrow of the
# Wide.TSpinbox layout for a given theme.
#------------------------------------------------------------------------------
proc ttk::wideSpinbox::UpdateElements theme {
    set bg  [NormalizeColor [ttk::style lookup . -background {} #d9d9d9]]
    variable onAndroid
    set aBg [expr {$onAndroid ? $bg :
	    [NormalizeColor [ttk::style lookup . -background active #ececec]]}]
    set fg  [NormalizeColor [ttk::style lookup . -foreground {} #000000]]
    set dFg [NormalizeColor [ttk::style lookup . -foreground disabled #a3a3a3]]

    if {$theme eq "aqua"} {
	scan $::tcl_platform(osVersion) "%d" majorOSVersion
	if {$majorOSVersion >= 18} {			;# OS X 10.14 or later
	    set pBg [NormalizeColor systemControlAccentColor]
	} else {
	    set pBg [NormalizeColor systemHighlightAlternate]
	}
	set pFg #ffffff
    } else {
	set pBg [ttk::style lookup . -selectbackground focus #000000]
	set pBg [NormalizeColor $pBg]
	set pFg [ttk::style lookup . -selectforeground focus #ffffff]
	set pFg [NormalizeColor $pFg]
    }

    set imgList [image names]

    # Update the WideSpinbox.uparrow element

    variable uparrowImgsArr
    foreach img $uparrowImgsArr($theme) {
	if {$img ni $imgList} {
	    return ""
	}
    }

    lassign $uparrowImgsArr($theme) img dImg pImg aImg
    variable uparrowImgData

    set imgData $uparrowImgData
    set idx [string first "bg" $imgData]
    set imgData [string replace $imgData $idx $idx+1 $bg]
    append imgData "stroke='$fg'/>\n</svg>"
    $img configure -data $imgData

    set imgData $uparrowImgData
    set imgData [string replace $imgData $idx $idx+1 $bg]
    append imgData "stroke='$dFg'/>\n</svg>"
    $dImg configure -data $imgData

    set imgData $uparrowImgData
    set imgData [string replace $imgData $idx $idx+1 $pBg]
    append imgData "stroke='$pFg'/>\n</svg>"
    $pImg configure -data $imgData

    set imgData $uparrowImgData
    set imgData [string replace $imgData $idx $idx+1 $aBg]
    append imgData "stroke='$fg'/>\n</svg>"
    $aImg configure -data $imgData

    # Update the WideSpinbox.downarrow element

    variable downarrowImgsArr
    foreach img $downarrowImgsArr($theme) {
	if {$img ni $imgList} {
	    return ""
	}
    }

    lassign $downarrowImgsArr($theme) img dImg pImg aImg
    variable downarrowImgData

    set imgData $downarrowImgData
    set idx [string first "bg" $imgData]
    set imgData [string replace $imgData $idx $idx+1 $bg]
    append imgData "stroke='$fg'/>\n</svg>"
    $img configure -data $imgData

    set imgData $downarrowImgData
    set imgData [string replace $imgData $idx $idx+1 $bg]
    append imgData "stroke='$dFg'/>\n</svg>"
    $dImg configure -data $imgData

    set imgData $downarrowImgData
    set imgData [string replace $imgData $idx $idx+1 $pBg]
    append imgData "stroke='$pFg'/>\n</svg>"
    $pImg configure -data $imgData

    set imgData $downarrowImgData
    set imgData [string replace $imgData $idx $idx+1 $aBg]
    append imgData "stroke='$fg'/>\n</svg>"
    $aImg configure -data $imgData
}

# Public procedures
# =================

#------------------------------------------------------------------------------
# ttk::wideSpinbox::CondMakeElements
#
# Creates the elements WideSpinbox.uparrow and WideSpinbox.downarrow of the
# Wide.TSpinbox layout if necessary.  Invoked from within the C code, by the
# ttk::spinbox widget initialization hook.
#------------------------------------------------------------------------------
proc ttk::wideSpinbox::CondMakeElements {} {
    variable madeElements
    if {!$madeElements} {
	# If Tk's scaling factor was changed via "tk scaling"
	# then $::tk::svgFmt now has the updated value.

	interp alias {} ::ttk::wideSpinbox::CreateImg \
		     {} image create photo -format $::tk::svgFmt
	variable gapImg [CreateImg -data {
<svg width="4" height="16" version="1.1" xmlns="http://www.w3.org/2000/svg"/>}]

	set theme [ttk::style theme use]
	CreateElements $theme
	UpdateElements $theme

	variable elemInfoArr
	set elemInfoArr($theme) 1

	set madeElements 1
    }
}

#------------------------------------------------------------------------------
# ttk::wideSpinbox::MakeOrUpdateElements
#
# Creates and/or updates the elements WideSpinbox.uparrow and
# WideSpinbox.downarrow of the Wide.TSpinbox layout for the current theme if
# necessary.  Invoked from within the procedures ttk::ThemeChanged and
# ttk::AppearanceChanged (see ttk.tcl).
#------------------------------------------------------------------------------
proc ttk::wideSpinbox::MakeOrUpdateElements {} {
    variable madeElements
    if {!$madeElements} {
	return ""
    }

    variable elemInfoArr
    set theme [ttk::style theme use]

    if {![info exists elemInfoArr($theme)]} {
	CreateElements $theme
	UpdateElements $theme
	set elemInfoArr($theme) 1
    } elseif {$theme eq "aqua"} {
	UpdateElements $theme
    }
}
