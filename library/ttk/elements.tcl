#==============================================================================
# elements.tcl - Copyright © 2025 Csaba Nemethi <csaba.nemethi@t-online.de>
#
# Contains procedures that create the Tglswitch*.trough and Tglswitch*.slider
# elements for the Toggleswitch* styles.
#
# Structure of the module:
#   - Private helper procedures and data
#   - Generic private procedures creating the elements for arbitrary themes
#   - Private procedures creating the elements for a few built-in themes
#   - Public procedures
#==============================================================================

# Private helper procedures and data
# ==================================

namespace eval ttk::toggleswitch {}

#------------------------------------------------------------------------------
# ttk::toggleswitch::Rgb2Hsv
#
# Converts the specified RGB value to HSV.  The argument is assumed to be of
# the form "#RRGGBB".  The return value is a list of the form {h s v}, where h
# in [0.0, 360.0) and s, v in [0.0, 100.0].
#------------------------------------------------------------------------------
proc ttk::toggleswitch::Rgb2Hsv rgb {
    scan $rgb "#%02x%02x%02x" r g b
    set r [expr {$r / 255.0}]
    set g [expr {$g / 255.0}]
    set b [expr {$b / 255.0}]

    set min [expr {min($r, $g, $b)}]
    set max [expr {max($r, $g, $b)}]
    set d [expr {$max - $min}]

    # Compute the saturation and value
    set s [expr {$max == 0 ? 0 : 100 * $d / $max}]
    set v [expr {100 * $max}]

    # Compute the hue
    if {$d == 0} {
	set h 0.0
    } elseif {$max == $r} {
	set frac [expr {fmod(($g - $b) / $d, 6)}]
	if {$frac < 0} { set frac [expr {$frac + 6}] }
	set h [expr {60 * $frac}]
    } elseif {$max == $g} {
	set h [expr {60 * (($b - $r) / $d + 2)}]
    } else {
	set h [expr {60 * (($r - $g) / $d + 4)}]
    }

    return [list $h $s $v]
}

#------------------------------------------------------------------------------
# ttk::toggleswitch::Hsv2Rgb
#
# Converts the specified HSV value to RGB.  The arguments are assumed to fulfil
# the conditions: h in [0.0, 360.0) and s, v in [0.0, 100.0].  The return value
# is a color specification of the form "#RRGGBB".
#------------------------------------------------------------------------------
proc ttk::toggleswitch::Hsv2Rgb {h s v} {
    set s [expr {$s / 100.0}]; set v [expr {$v / 100.0}]
    set c [expr {$s * $v}]				;# chroma
    set h [expr {$h / 60.0}]				;# in [0.0, 6.0)
    set x [expr {$c * (1 - abs(fmod($h, 2) - 1))}]	;# intermediate value

    switch [expr {int($h)}] {
	0 { set r $c;  set g $x;  set b  0 }
	1 { set r $x;  set g $c;  set b  0 }
	2 { set r  0;  set g $c;  set b $x }
	3 { set r  0;  set g $x;  set b $c }
	4 { set r $x;  set g  0;  set b $c }
	5 { set r $c;  set g  0;  set b $x }
    }

    set m [expr {$v - $c}]				;# lightness adjustment
    set r [expr {round(255 * ($r + $m))}]
    set g [expr {round(255 * ($g + $m))}]
    set b [expr {round(255 * ($b + $m))}]

    return [format "#%02x%02x%02x" $r $g $b]
}

#------------------------------------------------------------------------------
# ttk::toggleswitch::NormalizeColor
#
# Returns the representation of a given color in the form "#RRGGBB".
#------------------------------------------------------------------------------
proc ttk::toggleswitch::NormalizeColor color {
    lassign [winfo rgb . $color] r g b
    return [format "#%02x%02x%02x" \
	    [expr {$r >> 8}] [expr {$g >> 8}] [expr {$b >> 8}]]
}

#------------------------------------------------------------------------------
# ttk::toggleswitch::IsColorLight
#
# Checks whether a given color can be classified as light.
#------------------------------------------------------------------------------
proc ttk::toggleswitch::IsColorLight color {
    lassign [winfo rgb . $color] r g b
    return [expr {5 * ($g >> 8) + 2 * ($r >> 8) + ($b >> 8) > 8 * 192}]
}

interp alias {} ttk::toggleswitch::CreateElem {} ttk::style element create

namespace eval ttk::toggleswitch {
    variable troughData
    set troughData(1) {
<svg width="32" height="16" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <rect x="0" y="0" width="32" height="16" rx="8" }
    set troughData(2) {
<svg width="40" height="20" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <rect x="0" y="0" width="40" height="20" rx="10" }
    set troughData(3) {
<svg width="48" height="24" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <rect x="0" y="0" width="48" height="24" rx="12" }

    variable sliderData
    set sliderData(1) {
<svg width="16" height="12" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <circle cx="8" cy="6" r="6" }
    set sliderData(2) {
<svg width="20" height="16" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <circle cx="10" cy="8" r="8" }
    set sliderData(3) {
<svg width="24" height="20" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <circle cx="12" cy="10" r="10" }

    variable onAndroid	  [expr {[info exists ::tk::android] && $::tk::android}]
    variable madeElements 0
}

# Generic private procedures creating the elements for arbitrary themes
# =====================================================================

#------------------------------------------------------------------------------
# ttk::toggleswitch::CreateElements_genericLight
#------------------------------------------------------------------------------
proc ttk::toggleswitch::CreateElements_genericLight {} {
    variable troughData
    variable sliderData
    variable onAndroid

    set bg [ttk::style lookup . -background {} #d9d9d9]
    scan [NormalizeColor $bg] "#%02x%02x%02x" r g b
    set hasDarkerBg [expr {$r <= 0xd9 && $g <= 0xd9 && $b <= 0xd9}]

    set selBg [ttk::style lookup . -selectbackground {} #000000]
    if {[IsColorLight $selBg]} { set selBg #4a6984 }
    set selBg [NormalizeColor $selBg]

    foreach n {1 2 3} {
	# troughOffImg
	set imgData $troughData($n)
	set fill [expr {$hasDarkerBg ? "#c3c3c3" : "#d3d3d3"}]
	append imgData "fill='$fill'/>\n</svg>"
	set troughOffImg [CreateImg -data $imgData]

	# troughOffActiveImg
	set imgData $troughData($n)
	set fill2 [expr {$hasDarkerBg ? "#b3b3b3" : "#c3c3c3"}]
	set fill [expr {$onAndroid ? $fill : $fill2}]
	append imgData "fill='$fill'/>\n</svg>"
	set troughOffActiveImg [CreateImg -data $imgData]

	# troughOffPressedImg
	set imgData $troughData($n)
	set fill [expr {$hasDarkerBg ? "#a3a3a3" : "#b3b3b3"}]
	append imgData "fill='$fill'/>\n</svg>"
	set troughOffPressedImg [CreateImg -data $imgData]

	# troughOffDisabledImg
	set imgData $troughData($n)
	set fill [expr {$hasDarkerBg ? "#d1d1d1" : "#e1e1e1"}]
	append imgData "fill='$fill'/>\n</svg>"
	set troughOffDisabledImg [CreateImg -data $imgData]

	# troughOnImg
	set imgData $troughData($n)
	set fill $selBg
	lassign [Rgb2Hsv $fill] h s v
	set dv [expr {$v < 80 ? 10 : -10}]
	append imgData "fill='$fill'/>\n</svg>"
	set troughOnImg [CreateImg -data $imgData]

	# troughOnActiveImg
	set imgData $troughData($n)
	set v [expr {$v + $dv}]
	set fill [expr {$onAndroid ? $fill : [Hsv2Rgb $h $s $v]}]
	append imgData "fill='$fill'/>\n</svg>"
	set troughOnActiveImg [CreateImg -data $imgData]

	# troughOnPressedImg
	set imgData $troughData($n)
	set v [expr {$v + $dv}]
	append imgData "fill='[Hsv2Rgb $h $s $v]'/>\n</svg>"
	set troughOnPressedImg [CreateImg -data $imgData]

	# troughOnDisabledImg
	set imgData $troughData($n)
	append imgData "fill='[Hsv2Rgb $h 33.3 100]'/>\n</svg>"
	set troughOnDisabledImg [CreateImg -data $imgData]

	CreateElem Tglswitch$n.trough image [list $troughOffImg \
	    {selected disabled}	$troughOnDisabledImg \
	    {selected pressed}	$troughOnPressedImg \
	    {selected active}	$troughOnActiveImg \
	    selected		$troughOnImg \
	    disabled		$troughOffDisabledImg \
	    pressed		$troughOffPressedImg \
	    active		$troughOffActiveImg \
	]

	# sliderImg
	set imgData $sliderData($n)
	append imgData "fill='#ffffff'/>\n</svg>"
	set sliderImg [CreateImg -data $imgData]

	CreateElem Tglswitch$n.slider image $sliderImg
    }
}

#------------------------------------------------------------------------------
# ttk::toggleswitch::CreateElements_genericDark
#------------------------------------------------------------------------------
proc ttk::toggleswitch::CreateElements_genericDark {} {
    variable troughData
    variable sliderData
    variable onAndroid

    set selBg [ttk::style lookup . -selectbackground {} #000000]
    if {[IsColorLight $selBg]} { set selBg #4a6984 }
    set selBg [NormalizeColor $selBg]

    foreach n {1 2 3} {
	# troughOffImg
	set imgData $troughData($n)
	set fill "#585858"
	append imgData "fill='$fill'/>\n</svg>"
	set troughOffImg [CreateImg -data $imgData]

	# troughOffActiveImg
	set imgData $troughData($n)
	set fill [expr {$onAndroid ? $fill : "#676767"}]
	append imgData "fill='$fill'/>\n</svg>"
	set troughOffActiveImg [CreateImg -data $imgData]

	# troughOffPressedImg
	set imgData $troughData($n)
	append imgData "fill='#787878'/>\n</svg>"
	set troughOffPressedImg [CreateImg -data $imgData]

	# troughOffDisabledImg
	set imgData $troughData($n)
	append imgData "fill='#4a4a4a'/>\n</svg>"
	set troughOffDisabledImg [CreateImg -data $imgData]

	# troughOnImg
	set imgData $troughData($n)
	set fill $selBg
	lassign [Rgb2Hsv $fill] h s v
	set vOrig $v
	set dv [expr {$v < 80 ? 10 : -10}]
	append imgData "fill='$fill'/>\n</svg>"
	set troughOnImg [CreateImg -data $imgData]

	# troughOnActiveImg
	set imgData $troughData($n)
	set v [expr {$v + $dv}]
	set fill [expr {$onAndroid ? $fill : [Hsv2Rgb $h $s $v]}]
	append imgData "fill='$fill'/>\n</svg>"
	set troughOnActiveImg [CreateImg -data $imgData]

	# troughOnPressedImg
	set imgData $troughData($n)
	set v [expr {$v + $dv}]
	append imgData "fill='[Hsv2Rgb $h $s $v]'/>\n</svg>"
	set troughOnPressedImg [CreateImg -data $imgData]

	# troughOnDisabledImg
	set imgData $troughData($n)
	set v [expr {$vOrig - 10}]
	if {$v < 0} { set v 0 }
	append imgData "fill='[Hsv2Rgb $h $s $v]'/>\n</svg>"
	set troughOnDisabledImg [CreateImg -data $imgData]

	CreateElem Tglswitch$n.trough image [list $troughOffImg \
	    {selected disabled}	$troughOnDisabledImg \
	    {selected pressed}	$troughOnPressedImg \
	    {selected active}	$troughOnActiveImg \
	    selected		$troughOnImg \
	    disabled		$troughOffDisabledImg \
	    pressed		$troughOffPressedImg \
	    active		$troughOffActiveImg \
	]

	# sliderOffImg
	set imgData $sliderData($n)
	append imgData "fill='#d3d3d3'/>\n</svg>"
	set sliderOffImg [CreateImg -data $imgData]

	# sliderOffDisabledImg
	set imgData $sliderData($n)
	append imgData "fill='#888888'/>\n</svg>"
	set sliderOffDisabledImg [CreateImg -data $imgData]

	# sliderOnDisabledImg
	set imgData $sliderData($n)
	append imgData "fill='#9f9f9f'/>\n</svg>"
	set sliderOnDisabledImg [CreateImg -data $imgData]

	# sliderImg
	set imgData $sliderData($n)
	append imgData "fill='#ffffff'/>\n</svg>"
	set sliderImg [CreateImg -data $imgData]

	CreateElem Tglswitch$n.slider image [list $sliderOffImg \
	    {selected disabled}	$sliderOnDisabledImg \
	    selected		$sliderImg \
	    disabled		$sliderOffDisabledImg \
	    pressed		$sliderImg \
	    active		$sliderImg \
	]
    }
}

# Private procedures creating the elements for a few built-in themes
# ==================================================================

#------------------------------------------------------------------------------
# ttk::toggleswitch::CreateElements_clam
#------------------------------------------------------------------------------
proc ttk::toggleswitch::CreateElements_clam {} {
    variable troughData
    variable sliderData
    variable onAndroid

    foreach n {1 2 3} {
	# troughOffImg
	set imgData $troughData($n)
	set fill "#bab5ab"
	append imgData "fill='$fill'/>\n</svg>"
	set troughOffImg [CreateImg -data $imgData]

	# troughOffActiveImg
	set imgData $troughData($n)
	set fill [expr {$onAndroid ? $fill : "#aca79e"}]
	append imgData "fill='$fill'/>\n</svg>"
	set troughOffActiveImg [CreateImg -data $imgData]

	# troughOffPressedImg
	set imgData $troughData($n)
	append imgData "fill='#9e9a91'/>\n</svg>"
	set troughOffPressedImg [CreateImg -data $imgData]

	# troughOffDisabledImg
	set imgData $troughData($n)
	append imgData "fill='#cfc9be'/>\n</svg>"
	set troughOffDisabledImg [CreateImg -data $imgData]

	# troughOnImg
	set imgData $troughData($n)
	set fill [Hsv2Rgb 208 43.9 51.8]			;# #4a6984
	append imgData "fill='$fill'/>\n</svg>"
	set troughOnImg [CreateImg -data $imgData]

	# troughOnActiveImg
	set imgData $troughData($n)
	set fill [expr {$onAndroid ? $fill : [Hsv2Rgb 208 43.9 61.8]}]
	append imgData "fill='$fill'/>\n</svg>"
	set troughOnActiveImg [CreateImg -data $imgData]

	# troughOnPressedImg
	set imgData $troughData($n)
	append imgData "fill='[Hsv2Rgb 208 43.9 71.8]'/>\n</svg>"
	set troughOnPressedImg [CreateImg -data $imgData]

	# troughOnDisabledImg
	set imgData $troughData($n)
	append imgData "fill='[Hsv2Rgb 208 33.9 100]'/>\n</svg>"
	set troughOnDisabledImg [CreateImg -data $imgData]

	CreateElem Tglswitch$n.trough image [list $troughOffImg \
	    {selected disabled}	$troughOnDisabledImg \
	    {selected pressed}	$troughOnPressedImg \
	    {selected active}	$troughOnActiveImg \
	    selected		$troughOnImg \
	    disabled		$troughOffDisabledImg \
	    pressed		$troughOffPressedImg \
	    active		$troughOffActiveImg \
	]

	# sliderImg
	set imgData $sliderData($n)
	append imgData "fill='#ffffff'/>\n</svg>"
	set sliderImg [CreateImg -data $imgData]

	CreateElem Tglswitch$n.slider image $sliderImg
    }
}

#------------------------------------------------------------------------------
# ttk::toggleswitch::CreateElements_vista
#------------------------------------------------------------------------------
proc ttk::toggleswitch::CreateElements_vista {} {
    variable elemInfoArr
    if {[info exists elemInfoArr(vista)]} {
	return ""
    }

    if {$::tcl_platform(osVersion) >= 11.0} {			;# Win 11+
	CreateElements_win11
    } else {							;# Win 10-
	CreateElements_win10
    }

    foreach n {1 2 3} {
	ttk::style layout Toggleswitch$n [list \
	    Tglswitch.focus -sticky nswe -children [list \
		Tglswitch.padding -sticky nswe -children [list \
		    Tglswitch$n.trough -sticky {} -children [list \
			Tglswitch$n.slider -side left -sticky {}
		    ]
		]
	    ]
	]
    }

    set elemInfoArr(vista) 1
}

#------------------------------------------------------------------------------
# ttk::toggleswitch::CreateElements_win11
#------------------------------------------------------------------------------
proc ttk::toggleswitch::CreateElements_win11 {} {
    set troughOffData(1) {
<svg width="32" height="16" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <rect x="0.5" y="0.5" width="31" height="15" rx="7.5" }
    set troughOffData(2) {
<svg width="40" height="20" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <rect x="0.5" y="0.5" width="39" height="19" rx="9.5" }
    set troughOffData(3) {
<svg width="48" height="24" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <rect x="0.5" y="0.5" width="47" height="23" rx="11.5" }

    set troughOnData(1) {
<svg width="32" height="16" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <rect x="0" y="0" width="32" height="16" rx="8" }
    set troughOnData(2) {
<svg width="40" height="20" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <rect x="0" y="0" width="40" height="20" rx="10" }
    set troughOnData(3) {
<svg width="48" height="24" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <rect x="0" y="0" width="48" height="24" rx="12" }

    set sliderOffData(1) {
<svg width="16" height="10" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <circle cx="7" cy="5" r="4" }				;# margins L, R: 3, 5
    set sliderOffData(2) {
<svg width="20" height="14" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <circle cx="9" cy="7" r="6" }				;# margins L, R: 3, 5
    set sliderOffData(3) {
<svg width="24" height="18" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <circle cx="11" cy="9" r="8" }				;# margins L, R: 3, 5

    set sliderOnData(1) {
<svg width="16" height="10" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <circle cx="9" cy="5" r="4" }				;# margins L, R: 5, 3
    set sliderOnData(2) {
<svg width="20" height="14" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <circle cx="11" cy="7" r="6" }				;# margins L, R: 5, 3
    set sliderOnData(3) {
<svg width="24" height="18" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <circle cx="13" cy="9" r="8" }				;# margins L, R: 5, 3

    set sliderActiveData(1) {
<svg width="16" height="10" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <circle cx="8" cy="5" r="5" }				;# margins L, R: 3, 3
    set sliderActiveData(2) {
<svg width="20" height="14" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <circle cx="10" cy="7" r="7" }				;# margins L, R: 3, 3
    set sliderActiveData(3) {
<svg width="24" height="18" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <circle cx="12" cy="9" r="9" }				;# margins L, R: 3, 3

    set sliderOffPressedData(1) {
<svg width="16" height="10" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <rect x="3" y="0" width="13" height="10" rx="5" }	;# margins L, R: 3, 0
    set sliderOffPressedData(2) {
<svg width="20" height="14" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <rect x="3" y="0" width="17" height="14" rx="7" }	;# margins L, R: 3, 0
    set sliderOffPressedData(3) {
<svg width="24" height="18" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <rect x="3" y="0" width="21" height="18" rx="9" }	;# margins L, R: 3, 0

    set sliderOnPressedData(1) {
<svg width="16" height="10" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <rect x="0" y="0" width="13" height="10" rx="5" }	;# margins L, R: 0, 3
    set sliderOnPressedData(2) {
<svg width="20" height="14" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <rect x="0" y="0" width="17" height="14" rx="7" }	;# margins L, R: 0, 3
    set sliderOnPressedData(3) {
<svg width="24" height="18" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <rect x="0" y="0" width="21" height="18" rx="9" }	;# margins L, R: 0, 3

    foreach n {1 2 3} {
	# troughOffImg
	set imgData $troughOffData($n)
	append imgData "fill='#f6f6f6' stroke='#8a8a8a'/>\n</svg>"
	set troughOffImg [CreateImg -data $imgData]

	# troughOffActiveImg
	set imgData $troughOffData($n)
	append imgData "fill='#ededed' stroke='#878787'/>\n</svg>"
	set troughOffActiveImg [CreateImg -data $imgData]

	# troughOffPressedImg
	set imgData $troughOffData($n)
	append imgData "fill='#e4e4e4' stroke='#858585'/>\n</svg>"
	set troughOffPressedImg [CreateImg -data $imgData]

	# troughOffDisabledImg
	set imgData $troughOffData($n)
	append imgData "fill='#fbfbfb' stroke='#c5c5c5'/>\n</svg>"
	set troughOffDisabledImg [CreateImg -data $imgData]

	# troughOnImg
	set imgData $troughOnData($n)
	append imgData "fill='#005fb8'/>\n</svg>"
	set troughOnImg [CreateImg -data $imgData]

	# troughOnActiveImg
	set imgData $troughOnData($n)
	append imgData "fill='#196ebf'/>\n</svg>"
	set troughOnActiveImg [CreateImg -data $imgData]

	# troughOnPressedImg
	set imgData $troughOnData($n)
	append imgData "fill='#327ec5'/>\n</svg>"
	set troughOnPressedImg [CreateImg -data $imgData]

	# troughOnDisabledImg
	set imgData $troughOnData($n)
	append imgData "fill='#c5c5c5'/>\n</svg>"
	set troughOnDisabledImg [CreateImg -data $imgData]

	CreateElem Tglswitch$n.trough image [list $troughOffImg \
	    {selected disabled}	$troughOnDisabledImg \
	    {selected pressed}	$troughOnPressedImg \
	    {selected active}	$troughOnActiveImg \
	    selected		$troughOnImg \
	    disabled		$troughOffDisabledImg \
	    pressed		$troughOffPressedImg \
	    active		$troughOffActiveImg \
	]

	# sliderOffImg
	set imgData $sliderOffData($n)
	append imgData "fill='#5d5d5d'/>\n</svg>"
	set sliderOffImg [CreateImg -data $imgData]

	# sliderOffActiveImg
	set imgData $sliderActiveData($n)
	append imgData "fill='#5a5a5a'/>\n</svg>"
	set sliderOffActiveImg [CreateImg -data $imgData]

	# sliderOffPressedImg
	set imgData $sliderOffPressedData($n)
	append imgData "fill='#575757'/>\n</svg>"
	set sliderOffPressedImg [CreateImg -data $imgData]

	# sliderOffDisabledImg
	set imgData $sliderOffData($n)
	append imgData "fill='#a1a1a1'/>\n</svg>"
	set sliderOffDisabledImg [CreateImg -data $imgData]

	# sliderOnImg
	set imgData $sliderOnData($n)
	append imgData "fill='#ffffff'/>\n</svg>"
	set sliderOnImg [CreateImg -data $imgData]

	# sliderOnActiveImg
	set imgData $sliderActiveData($n)
	append imgData "fill='#ffffff'/>\n</svg>"
	set sliderOnActiveImg [CreateImg -data $imgData]

	# sliderOnPressedImg
	set imgData $sliderOnPressedData($n)
	append imgData "fill='#ffffff'/>\n</svg>"
	set sliderOnPressedImg [CreateImg -data $imgData]

	# sliderOnDisabledImg
	set imgData $sliderOnData($n)
	append imgData "fill='#ffffff'/>\n</svg>"
	set sliderOnDisabledImg [CreateImg -data $imgData]

	CreateElem Tglswitch$n.slider image [list $sliderOffImg \
	    {selected disabled}	$sliderOnDisabledImg \
	    {selected pressed}	$sliderOnPressedImg \
	    {selected active}	$sliderOnActiveImg \
	    selected		$sliderOnImg \
	    disabled		$sliderOffDisabledImg \
	    pressed		$sliderOffPressedImg \
	    active		$sliderOffActiveImg \
	]
    }
}

#------------------------------------------------------------------------------
# ttk::toggleswitch::CreateElements_win10
#------------------------------------------------------------------------------
proc ttk::toggleswitch::CreateElements_win10 {} {
    set troughOffData(1) {
<svg width="35" height="16" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <rect x="1" y="1" width="33" height="14" rx="7" stroke-width="2" }
    set troughOffData(2) {
<svg width="44" height="20" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <rect x="1" y="1" width="42" height="18" rx="9" stroke-width="2" }
    set troughOffData(3) {
<svg width="53" height="24" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <rect x="1" y="1" width="51" height="22" rx="11" stroke-width="2" }

    set troughOnData(1) {
<svg width="35" height="16" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <rect x="0" y="0" width="35" height="16" rx="8" }
    set troughOnData(2) {
<svg width="44" height="20" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <rect x="0" y="0" width="44" height="20" rx="10" }
    set troughOnData(3) {
<svg width="53" height="24" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <rect x="0" y="0" width="53" height="24" rx="12" }

    set troughPressedData(1) {
<svg width="35" height="16" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <rect x="0" y="0" width="35" height="16" rx="8" fill="#666666"/>
</svg>}
    set troughPressedData(2) {
<svg width="44" height="20" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <rect x="0" y="0" width="44" height="20" rx="10" fill="#666666"/>
</svg>}
    set troughPressedData(3) {
<svg width="53" height="24" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <rect x="0" y="0" width="53" height="24" rx="12" fill="#666666"/>
</svg>}

    set sliderData(1) {
<svg width="16" height="8" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <circle cx="8" cy="4" r="4" }
    set sliderData(2) {
<svg width="20" height="10" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <circle cx="10" cy="5" r="5" }
    set sliderData(3) {
<svg width="24" height="12" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <circle cx="12" cy="6" r="6" }

    foreach n {1 2 3} {
	# troughOffImg
	set imgData $troughOffData($n)
	append imgData "fill='#ffffff' stroke='#333333'/>\n</svg>"
	set troughOffImg [CreateImg -data $imgData]

	# troughOffDisabledImg
	set imgData $troughOffData($n)
	append imgData "fill='#ffffff' stroke='#999999'/>\n</svg>"
	set troughOffDisabledImg [CreateImg -data $imgData]

	# troughOnImg
	set imgData $troughOnData($n)
	append imgData "fill='#0078d7'/>\n</svg>"
	set troughOnImg [CreateImg -data $imgData]

	# troughOnActiveImg
	set imgData $troughOnData($n)
	append imgData "fill='#4da1e3'/>\n</svg>"
	set troughOnActiveImg [CreateImg -data $imgData]

	# troughOnDisabledImg
	set imgData $troughOnData($n)
	append imgData "fill='#cccccc'/>\n</svg>"
	set troughOnDisabledImg [CreateImg -data $imgData]

	# troughPressedImg
	set troughPressedImg [CreateImg -data $troughPressedData($n)]

	CreateElem Tglswitch$n.trough image [list $troughOffImg \
	    {selected disabled}	$troughOnDisabledImg \
	    {selected pressed}	$troughPressedImg \
	    {selected active}	$troughOnActiveImg \
	    selected		$troughOnImg \
	    disabled		$troughOffDisabledImg \
	    pressed		$troughPressedImg \
	]

	# sliderOffImg
	set imgData $sliderData($n)
	append imgData "fill='#333333'/>\n</svg>"
	set sliderOffImg [CreateImg -data $imgData]

	# sliderOffDisabledImg
	set imgData $sliderData($n)
	append imgData "fill='#999999'/>\n</svg>"
	set sliderOffDisabledImg [CreateImg -data $imgData]

	# sliderOnImg
	set imgData $sliderData($n)
	append imgData "fill='#ffffff'/>\n</svg>"
	set sliderOnImg [CreateImg -data $imgData]

	# sliderOnDisabledImg
	set imgData $sliderData($n)
	append imgData "fill='#a3a3a3'/>\n</svg>"
	set sliderOnDisabledImg [CreateImg -data $imgData]

	# sliderPressedImg
	set imgData $sliderData($n)
	append imgData "fill='#ffffff'/>\n</svg>"
	set sliderPressedImg [CreateImg -data $imgData]

	CreateElem Tglswitch$n.slider image [list $sliderOffImg \
	    {selected disabled}	$sliderOnDisabledImg \
	    selected		$sliderOnImg \
	    disabled		$sliderOffDisabledImg \
	    pressed		$sliderPressedImg \
	]
    }
}

#------------------------------------------------------------------------------
# ttk::toggleswitch::CreateElements_aqua
#------------------------------------------------------------------------------
proc ttk::toggleswitch::CreateElements_aqua {} {
    variable troughImgArr
    variable sliderImgArr

    foreach n {1 2 3} {
	foreach state {off offPressed offDisabled
		       on onPressed onDisabled onBg onDisabledBg} {
	    set troughImgArr(${state}$n) [CreateImg]
	}

	CreateElem Tglswitch$n.trough image [list $troughImgArr(off$n) \
	    {selected disabled background}	$troughImgArr(onDisabledBg$n) \
	    {selected disabled}			$troughImgArr(onDisabled$n) \
	    {selected background}		$troughImgArr(onBg$n) \
	    {selected pressed}			$troughImgArr(onPressed$n) \
	    selected				$troughImgArr(on$n) \
	    disabled				$troughImgArr(offDisabled$n) \
	    pressed				$troughImgArr(offPressed$n) \
	]

	foreach state {off offPressed offDisabled
		       on onPressed onDisabled} {
	    set sliderImgArr(${state}$n) [CreateImg]
	}

	CreateElem Tglswitch$n.slider image [list $sliderImgArr(off$n) \
	    {selected disabled}		$sliderImgArr(onDisabled$n) \
	    {selected pressed}		$sliderImgArr(onPressed$n) \
	    selected			$sliderImgArr(on$n) \
	    disabled			$sliderImgArr(offDisabled$n) \
	    pressed			$sliderImgArr(offPressed$n) \
	]
    }

    UpdateElements_aqua
}

#------------------------------------------------------------------------------
# ttk::toggleswitch::UpdateElements_aqua
#------------------------------------------------------------------------------
proc ttk::toggleswitch::UpdateElements_aqua {} {
    scan $::tcl_platform(osVersion) "%d" majorOSVersion
    if {$majorOSVersion >= 25} {			;# macOS 26 or later
	UpdateElements_aqua_macos26
    } else {						;# macOS 15 or earlier
	UpdateElements_aqua_macos15
    }

    foreach n {1 2 3} {
	ttk::style layout Toggleswitch$n [list \
	    Tglswitch.padding -sticky nswe -children [list \
		Tglswitch$n.trough -sticky {} -children [list \
		    Tglswitch$n.slider -side left -sticky {} \
		]
	    ]
	]
    }
}

#------------------------------------------------------------------------------
# ttk::toggleswitch::UpdateElements_aqua_macos26
#------------------------------------------------------------------------------
proc ttk::toggleswitch::UpdateElements_aqua_macos26 {} {
    variable troughImgArr
    variable sliderImgArr

    set troughOffData(1) {
<svg width="29" height="13" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <rect x="0" y="0" width="29" height="13" rx="6.5" }
    set troughOffData(2) {
<svg width="36" height="16" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <rect x="0" y="0" width="36" height="16" rx="8" }
    set troughOffData(3) {
<svg width="44" height="20" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <rect x="0" y="0" width="44" height="20" rx="10" }

    set troughOnData(1) $troughOffData(1)
    set troughOnData(2) $troughOffData(2)
    set troughOnData(3) $troughOffData(3)

    set sliderOffData(1) {
<svg width="19" height="13" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <rect x="0.5" y="0.5" width="18" height="12" rx="6" }
    set sliderOffData(2) {
<svg width="23" height="16" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <rect x="0.5" y="0.5" width="22" height="15" rx="7.5" }
    set sliderOffData(3) {
<svg width="30" height="20" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <rect x="1" y="1" width="28" height="18" rx="9" stroke-width="2" }

    set sliderOnData(1) {
<svg width="19" height="13" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <rect x="1" y="1" width="17" height="11" rx="5.5" }
    set sliderOnData(2) {
<svg width="23" height="16" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <rect x="1" y="1" width="21" height="14" rx="7" }
    set sliderOnData(3) {
<svg width="30" height="20" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <rect x="2" y="2" width="26" height="16" rx="8" }

    set darkMode    [tk::unsupported::MacWindowStyle isdark .]
    set selectBg    [NormalizeColor systemSelectedContentBackgroundColor]
    set accentColor [NormalizeColor systemControlAccentColor]

    foreach n {1 2 3} {
	# troughImgArr(off$n)
	set imgData $troughOffData($n)
	set fill [expr {$darkMode ? "#3e4345" : "#dddddd"}]
	append imgData "fill='$fill'/>\n</svg>"
	$troughImgArr(off$n) configure -data $imgData

	# troughImgArr(offPressed$n)
	set imgData $troughOffData($n)
	set fill [expr {$darkMode ? "#525759" : "#c5c6c7"}]
	append imgData "fill='$fill'/>\n</svg>"
	$troughImgArr(offPressed$n) configure -data $imgData

	# troughImgArr(offDisabled$n)
	set imgData $troughOffData($n)
	set fill [expr {$darkMode ? "#343938" : "#e9e9e9"}]
	append imgData "fill='$fill'/>\n</svg>"
	$troughImgArr(offDisabled$n) configure -data $imgData

	# troughImgArr(on$n)
	set imgData $troughOnData($n)
	set fill $accentColor
	append imgData "fill='$fill'/>\n</svg>"
	$troughImgArr(on$n) configure -data $imgData

	# troughImgArr(onPressed$n)
	set imgData $troughOnData($n)
	set fill $selectBg
	append imgData "fill='$fill'/>\n</svg>"
	$troughImgArr(onPressed$n) configure -data $imgData

	# troughImgArr(onDisabled$n)
	set imgData $troughOnData($n)
	set fill [NormalizeColor systemSelectedControlColor]
	append imgData "fill='$fill'/>\n</svg>"
	$troughImgArr(onDisabled$n) configure -data $imgData

	# troughImgArr(onBg$n)
	set imgData $troughOnData($n)
	set fill [expr {$darkMode ? "#4b4c4d" : "#d2d2d2"}]
	append imgData "fill='$fill'/>\n</svg>"
	$troughImgArr(onBg$n) configure -data $imgData

	# troughImgArr(onDisabledBg$n)
	set imgData $troughOnData($n)
	set fill [expr {$darkMode ? "#393e3f" : "#e3e3e3"}]
	append imgData "fill='$fill'/>\n</svg>"
	$troughImgArr(onDisabledBg$n) configure -data $imgData

	# sliderImgArr(off$n)
	set imgData $sliderOffData($n)
	set fill [expr {$darkMode ? "#e2e3e3" : "#ffffff"}]
	set strk [expr {$darkMode ? "#3c4041" : "#d4d4d4"}]
	append imgData "fill='$fill' stroke='$strk'/>\n</svg>"
	$sliderImgArr(off$n) configure -data $imgData

	# sliderImgArr(offPressed$n)
	set imgData $sliderOffData($n)
	set fill [expr {$darkMode ? "#c2c3c3" : "#efefef"}]
	set strk [expr {$darkMode ? "#1c2021" : "#c4c4c4"}]
	append imgData "fill='$fill' stroke='$strk'/>\n</svg>"
	$sliderImgArr(offPressed$n) configure -data $imgData

	# sliderImgArr(offDisabled$n)
	set imgData $sliderOffData($n)
	set fill [expr {$darkMode ? "#e1e2e2" : "#ffffff"}]
	set strk [expr {$darkMode ? "#323739" : "#e0e0e0"}]
	append imgData "fill='$fill' stroke='$strk'/>\n</svg>"
	$sliderImgArr(offDisabled$n) configure -data $imgData

	# sliderImgArr(on$n)
	set imgData $sliderOnData($n)
	set fill [expr {$darkMode ? "#d9ecff" : "#ffffff"}]
	append imgData "fill='$fill'/>\n</svg>"
	$sliderImgArr(on$n) configure -data $imgData

	# sliderImgArr(onPressed$n)
	set imgData $sliderOnData($n)
	set fill [expr {$darkMode ? "#c2c3c3" : "#efefef"}]
	append imgData "fill='$fill'/>\n</svg>"
	$sliderImgArr(onPressed$n) configure -data $imgData

	# sliderImgArr(onDisabled$n)
	set imgData $sliderOnData($n)
	set fill [expr {$darkMode ? "#dde5ee" : "#ffffff"}]
	append imgData "fill='$fill'/>\n</svg>"
	$sliderImgArr(onDisabled$n) configure -data $imgData
    }
}

#------------------------------------------------------------------------------
# ttk::toggleswitch::UpdateElements_aqua_macos15
#------------------------------------------------------------------------------
proc ttk::toggleswitch::UpdateElements_aqua_macos15 {} {
    variable troughImgArr
    variable sliderImgArr

    set troughOffData(1) {
<svg width="26" height="15" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <rect x="0.5" y="0.5" width="25" height="14" rx="7" }
    set troughOffData(2) {
<svg width="32" height="18" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <rect x="0.5" y="0.5" width="31" height="17" rx="8.5" }
    set troughOffData(3) {
<svg width="38" height="22" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <rect x="0.5" y="0.5" width="37" height="21" rx="10.5" }

    set troughOnData(1) {
<svg width="26" height="15" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <rect x="0" y="0" width="26" height="15" rx="7.5" }
    set troughOnData(2) {
<svg width="32" height="18" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <rect x="0" y="0" width="32" height="18" rx="9" }
    set troughOnData(3) {
<svg width="38" height="22" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <rect x="0" y="0" width="38" height="22" rx="11" }

    set sliderOffData(1) {
<svg width="15" height="15" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <circle cx="7.5" cy="7.5" r="7" }
    set sliderOffData(2) {
<svg width="18" height="18" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <circle cx="9" cy="9" r="8.5" }
    set sliderOffData(3) {
<svg width="22" height="22" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <circle cx="11" cy="11" r="10.5" }

    set sliderOnData(1) {
<svg width="15" height="15" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <circle cx="7.5" cy="7.5" r="6.5" }
    set sliderOnData(2) {
<svg width="18" height="18" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <circle cx="9" cy="9" r="8" }
    set sliderOnData(3) {
<svg width="22" height="22" version="1.1" xmlns="http://www.w3.org/2000/svg">
 <circle cx="11" cy="11" r="10" }

    set darkMode [tk::unsupported::MacWindowStyle isdark .]
    scan $::tcl_platform(osVersion) "%d" majorOSVersion
    if {$majorOSVersion >= 18} {			;# OS X 10.14 or later
	set selectBg    systemSelectedContentBackgroundColor
	set accentColor systemControlAccentColor
    } else {
	set selectBg    systemHighlightAlternate
	set accentColor systemHighlightAlternate
    }
    set selectBg    [NormalizeColor $selectBg]
    set accentColor [NormalizeColor $accentColor]

    foreach n {1 2 3} {
	# troughImgArr(off$n)
	set imgData $troughOffData($n)
	set fill [expr {$darkMode ? "#414141" : "#d9d9d9"}]
	set strk [expr {$darkMode ? "#606060" : "#cdcdcd"}]
	append imgData "fill='$fill' stroke='$strk'/>\n</svg>"
	$troughImgArr(off$n) configure -data $imgData

	# troughImgArr(offPressed$n)
	set imgData $troughOffData($n)
	set fill [expr {$darkMode ? "#4d4d4d" : "#cbcbcb"}]
	set strk [expr {$darkMode ? "#6a6a6a" : "#c0c0c0"}]
	append imgData "fill='$fill' stroke='$strk'/>\n</svg>"
	$troughImgArr(offPressed$n) configure -data $imgData

	# troughImgArr(offDisabled$n)
	set imgData $troughOffData($n)
	set fill [expr {$darkMode ? "#282828" : "#f4f4f4"}]
	set strk [expr {$darkMode ? "#393939" : "#ededed"}]
	append imgData "fill='$fill' stroke='$strk'/>\n</svg>"
	$troughImgArr(offDisabled$n) configure -data $imgData

	# troughImgArr(on$n)
	set imgData $troughOnData($n)
	set fill [expr {$darkMode ? $selectBg : $accentColor}]
	if {$darkMode} {
	    # For the colors blue, purple, pink, red, orange, yellow, green,
	    # and graphite replace $fill with its counterpart for LightAqua
	    array set tmpArr {
		#0059d1 #0064e1  #803482 #7d2a7e  #c93379 #d93b86
		#d13539 #c4262b  #c96003 #d96b0a  #d19e00 #e1ac15
		#43932a #4da033  #696969 #808080

		#0058d0 #007aff  #7f3280 #953d96  #c83179 #f74f9e
		#d03439 #e0383e  #c86003 #f7821b  #cd8f0e #fcb827
		#42912a #62ba46  #686868 #989898
	    }
	    if {[info exists tmpArr($fill)]} { set fill $tmpArr($fill) }
	    array unset tmpArr
	}
	append imgData "fill='$fill'/>\n</svg>"
	$troughImgArr(on$n) configure -data $imgData

	# troughImgArr(onPressed$n)
	set imgData $troughOnData($n)
	set fill [expr {$darkMode ? $accentColor : $selectBg}]
	if {$darkMode} {
	    # For the colors purple, red, yellow, and graphite
	    # replace $fill with its counterpart for LightAqua
	    array set tmpArr {
		#a550a7 #953d96  #ff5257 #e0383e
		#ffc600 #ffc726  #8c8c8c #989898

		#a550a7 #7d2a7e  #f74f9e #d93b85
		#fcb827 #de9e15  #8c8c8c #808080
	    }
	    if {[info exists tmpArr($fill)]} { set fill $tmpArr($fill) }
	    array unset tmpArr
	}
	append imgData "fill='$fill'/>\n</svg>"
	$troughImgArr(onPressed$n) configure -data $imgData

	# troughImgArr(onDisabled$n)
	set imgData $troughOnData($n)
	set fill [NormalizeColor systemSelectedControlColor]
	append imgData "fill='$fill'/>\n</svg>"
	$troughImgArr(onDisabled$n) configure -data $imgData

	# troughImgArr(onBg$n)
	set imgData $troughOnData($n)
	set fill [expr {$darkMode ? "#676665" : "#b0b0b0"}]
	append imgData "fill='$fill'/>\n</svg>"
	$troughImgArr(onBg$n) configure -data $imgData

	# troughImgArr(onDisabledBg$n)
	set imgData $troughOnData($n)
	set fill [expr {$darkMode ? "#282828" : "#f4f4f4"}]
	append imgData "fill='$fill'/>\n</svg>"
	$troughImgArr(onDisabledBg$n) configure -data $imgData

	# sliderImgArr(off$n)
	set imgData $sliderOffData($n)
	set fill [expr {$darkMode ? "#cacaca" : "#ffffff"}]
	set strk [expr {$darkMode ? "#606060" : "#cdcdcd"}]
	append imgData "fill='$fill' stroke='$strk'/>\n</svg>"
	$sliderImgArr(off$n) configure -data $imgData

	# sliderImgArr(offPressed$n)
	set imgData $sliderOffData($n)
	set fill [expr {$darkMode ? "#e4e4e4" : "#f0f0f0"}]
	set strk [expr {$darkMode ? "#6a6a6a" : "#c0c0c0"}]
	append imgData "fill='$fill' stroke='$strk'/>\n</svg>"
	$sliderImgArr(offPressed$n) configure -data $imgData

	# sliderImgArr(offDisabled$n)
	set imgData $sliderOffData($n)
	set fill [expr {$darkMode ? "#595959" : "#fdfdfd"}]
	set strk [expr {$darkMode ? "#393939" : "#ededed"}]
	append imgData "fill='$fill' stroke='$strk'/>\n</svg>"
	$sliderImgArr(offDisabled$n) configure -data $imgData

	# sliderImgArr(on$n)
	set imgData $sliderOnData($n)
	set fill [expr {$darkMode ? "#cacaca" : "#ffffff"}]
	append imgData "fill='$fill'/>\n</svg>"
	$sliderImgArr(on$n) configure -data $imgData

	# sliderImgArr(onPressed$n)
	set imgData $sliderOnData($n)
	set fill [expr {$darkMode ? "#e4e4e4" : "#f0f0f0"}]
	append imgData "fill='$fill'/>\n</svg>"
	$sliderImgArr(onPressed$n) configure -data $imgData

	# sliderImgArr(onDisabled$n)
	set imgData $sliderOnData($n)
	set fill [expr {$darkMode ? "#595959" : "#fdfdfd"}]
	append imgData "fill='$fill'/>\n</svg>"
	$sliderImgArr(onDisabled$n) configure -data $imgData
    }
}

#------------------------------------------------------------------------------
# ttk::toggleswitch::CreateElements
#
# Creates the Tglswitch*.trough and Tglswitch*.slider elements for the
# Toggleswitch* styles if they don't yet exist.  Invoked by the procedures
# ttk::toggleswitch::CondMakeElements and ttk::toggleswitch::CondUpdateElements
# below.
#------------------------------------------------------------------------------
proc ttk::toggleswitch::CreateElements {} {
    set theme [ttk::style theme use]

    variable elemInfoArr
    if {[info exists elemInfoArr($theme)]} {
	if {$theme eq "aqua"} {
	    UpdateElements_$theme
	}

	return ""
    }

    switch $theme {
	clam - vista - aqua {
	    CreateElements_$theme
	}
	winnative {
	    ttk::style theme settings vista { CreateElements_vista }
	    foreach n {1 2 3} {
		CreateElem Tglswitch$n.trough from vista
		CreateElem Tglswitch$n.slider from vista
	    }
	}
	default {
	    if {[llength [info commands CreateElements_$theme]] == 1} {
		# The application can provide its own
		# ttk::toggleswitch::CreateElements_$theme command.
		#
		CreateElements_$theme
	    } else {
		set fg [ttk::style lookup . -foreground {} #000000]
		if {[IsColorLight $fg]} {
		    CreateElements_genericDark
		} else {
		    CreateElements_genericLight
		}
	    }
	}
    }
    set elemInfoArr($theme) 1

    if {$theme eq "aqua"} {
	foreach n {1 2 3} {
	    ttk::style layout Toggleswitch$n [list \
		Tglswitch.padding -sticky nswe -children [list \
		    Tglswitch$n.trough -sticky {} -children [list \
			Tglswitch$n.slider -side left -sticky {} \
		    ]
		]
	    ]

	    if {[ttk::style lookup Toggleswitch$n -padding] eq ""} {
		ttk::style configure Toggleswitch$n -padding 1.5p
	    }
	}
    } else {
	foreach n {1 2 3} {
	    ttk::style layout Toggleswitch$n [list \
		Tglswitch.focus -sticky nswe -children [list \
		    Tglswitch.padding -sticky nswe -children [list \
			Tglswitch$n.trough -sticky {} -children [list \
			    Tglswitch$n.slider -side left -sticky {}
			]
		    ]
		]
	    ]

	    if {[ttk::style lookup Toggleswitch$n -padding] eq ""} {
		ttk::style configure Toggleswitch$n -padding 0.75p
	    }
	    if {$theme eq "classic" &&
		    [ttk::style lookup Toggleswitch$n -focussolid] eq ""} {
		ttk::style configure Toggleswitch$n -focussolid 1
	    }
	}
    }
}

# Public procedures
# =================

#------------------------------------------------------------------------------
# ttk::toggleswitch::CondMakeElements
#
# Creates the Tglswitch*.trough and Tglswitch*.slider elements for the
# Toggleswitch* styles if necessary.  Invoked from within the C code, by the
# widget initialization hook.
#------------------------------------------------------------------------------
proc ttk::toggleswitch::CondMakeElements {} {
    variable madeElements
    if {!$madeElements} {
	# If Tk's scaling factor was changed via "tk scaling"
	# then $::tk::svgFmt now has the updated value.

	interp alias {} ::ttk::toggleswitch::CreateImg \
		     {} image create photo -format $::tk::svgFmt

	CreateElements
	set madeElements 1
    }
}

#------------------------------------------------------------------------------
# ttk::toggleswitch::CondUpdateElements
#
# Updates the Tglswitch*.trough and Tglswitch*.slider elements for the
# Toggleswitch* styles if necessary.  Invoked from within the procedures
# ttk::ThemeChanged and ttk::AppearanceChanged (see ttk.tcl).
#------------------------------------------------------------------------------
proc ttk::toggleswitch::CondUpdateElements {} {
    variable madeElements
    if {$madeElements} {		;# for some theme and appearance
	CreateElements			;# for the new theme or appearance
    }
}
