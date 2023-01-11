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

variable ::tk::svgFmt [list svg -scale [expr {[::tk::ScalingPct] / 100.0}]]

image create photo ::tk::icons::error -format $::tk::svgFmt -data {
    <?xml version="1.0" encoding="UTF-8"?>
    <svg width="32" height="32" version="1.1" viewBox="0 0 8.4669 8.4669" xmlns="http://www.w3.org/2000/svg">
     <g transform="matrix(1.4545 0 0 1.4545 5.0036 -423.15)">
      <rect transform="matrix(0,-1,-1,0,0,0)" x="-296.73" y="-2.381" width="5.821" height="5.821" rx="2.91" ry="2.91" fill="#d32f2f"/>
      <path d="m-1.587 292.77 2.116 2.116m1e-3 -2.116-2.118 2.116" fill="none" stroke="#fff" stroke-linecap="square" stroke-width=".661"/>
     </g>
    </svg>
}

image create photo ::tk::icons::warning -format $::tk::svgFmt -data {
    <?xml version="1.0" encoding="UTF-8"?>
    <svg width="32" height="32" version="1.1" xmlns="http://www.w3.org/2000/svg">
     <style id="current-color-scheme" type="text/css">.ColorScheme-NeutralText {
		color:#f67400;
	    }
	    .ColorScheme-Text {
		color:#232629;
	    }</style>
     <g stroke-width="2">
      <circle transform="scale(1,-1)" cx="16" cy="-16" r="16" fill="#f67400"/>
      <circle cx="16" cy="24" r="2" fill="#fff"/>
      <path d="m14 20h4v-14h-4z" fill="#fff" fill-rule="evenodd"/>
     </g>
    </svg>
}

image create photo ::tk::icons::information -format $::tk::svgFmt -data {
    <?xml version="1.0" encoding="UTF-8"?>
    <svg width="32" height="32" version="1.1" xmlns="http://www.w3.org/2000/svg">
     <g stroke-width="2">
      <circle transform="scale(1,-1)" cx="16" cy="-16" r="16" fill="#2091df"/>
      <circle transform="scale(1,-1)" cx="16" cy="-8" r="2" fill="#fff"/>
      <path d="m14 12h4v14h-4z" fill="#fff" fill-rule="evenodd"/>
     </g>
    </svg>
}

image create photo ::tk::icons::question -format $::tk::svgFmt -data {
    <?xml version="1.0" encoding="UTF-8"?>
    <svg width="32" height="32" version="1" xmlns="http://www.w3.org/2000/svg">
     <g transform="matrix(.8 0 0 .8 -3.2 -3.2)">
      <circle cx="24" cy="24" r="20" fill="#78c367"/>
      <path d="m26 38h-4v-4h4zm4.14-15.5-1.8 1.84c-1.44 1.46-2.34 2.66-2.34 5.66h-4v-1c0-2.2 0.9-4.2 2.34-5.66l2.48-2.52a3.91 3.91 0 0 0 1.18-2.82c0-2.2-1.8-4-4-4s-4 1.8-4 4h-4c0-4.42 3.58-8 8-8s8 3.58 8 8c0 1.76-0.72 3.36-1.86 4.5z" fill="#fff"/>
     </g>
    </svg>
}

set ::tk::icons::checkbox {
<?xml version="1.0" encoding="UTF-8" standalone="no"?>
<svg
   width="13"
   height="13"
   version="1.1"
   id="classiccheckbox"
   xmlns:svg="http://www.w3.org/2000/svg">
  <defs id="defs325">
    <linearGradient id="linearGradient2676">
      <stop
         style="stop-color:$SELECT;stop-opacity:1;"
         offset="0"
         id="stop2672" />
      <stop
         style="stop-color:$DARK;stop-opacity:0;"
         offset="1"
         id="stop2674" />
    </linearGradient>
    <linearGradient
       id="linearGradient1889">
      <stop
         style="stop-color:$BACKGROUND;stop-opacity:0;"
         offset="0"
         id="stop1885" />
      <stop
         style="stop-color:$DARK;stop-opacity:1;"
         offset="1"
         id="stop1887" />
    </linearGradient>
    <linearGradient
       id="linearGradient1695">
      <stop
         style="stop-color:$SELECT;stop-opacity:1;"
         offset="0"
         id="stop1691" />
      <stop
         style="stop-color:$BACKGROUND;stop-opacity:0;"
         offset="1"
         id="stop1693" />
    </linearGradient>
    <linearGradient
       xlink:href="#linearGradient1889"
       id="linearGradient1891"
       x1="-1"
       y1="6.5"
       x2="1"
       y2="6.5"
       gradientUnits="userSpaceOnUse" />
    <linearGradient
       xlink:href="#linearGradient1695"
       id="linearGradient2162"
       gradientUnits="userSpaceOnUse"
       x1="6.5"
       y1="14"
       x2="6.5"
       y2="12" />
    <linearGradient
       xlink:href="#linearGradient2676"
       id="linearGradient2678"
       x1="14"
       y1="6.5"
       x2="12"
       y2="6.5"
       gradientUnits="userSpaceOnUse" />
    <linearGradient
       xlink:href="#linearGradient1889"
       id="linearGradient3823"
       x1="6.5"
       y1="-1"
       x2="6.5"
       y2="1"
       gradientUnits="userSpaceOnUse" />
  </defs>
  <g
     id="layer1">
    <rect
       style="fill:$SELECT;stroke-width:11;paint-order:stroke markers fill"
       id="rectbackdrop"
       width="11"
       height="11"
       x="1"
       y="1" />
    <path
       id="rectleftside"
       style="stroke-width:10.16;paint-order:stroke markers fill;fill-opacity:1;fill:url(#linearGradient1891)"
       d="M 0,0 L 1,1 1,12 0,13" />
    <path
       id="recttopside"
       style="stroke-width:10.16;paint-order:stroke markers fill;fill-opacity:1;fill:url(#linearGradient3823)"
       d="M 0,0 L 13,0 12,1 1,1 0,0" />
    <path
       id="rectrightside"
       style="stroke-width:10.16;paint-order:stroke markers fill;fill-opacity:1;fill:url(#linearGradient2678)"
       d="M 13,0 L 12,1 12,12 13,13 13,0" />
    <path
       id="rectbottomside"
       style="stroke-width:10.16;paint-order:stroke markers fill;fill-opacity:1;fill:url(#linearGradient2162)"
       d="M 0,13 L 1,12 12,12 13,13 0,13" />
    <path
       style="fill:none;stroke:$INDICATOR;stroke-width:2px;stroke-linecap:butt;stroke-linejoin:miter;paint-order:stroke markers fill;stroke-opacity:1"
       d="M 3,7 L 6,10 10,3"
       id="indicator" />
  </g>
</svg>
}

set ::tk::icons::radiobox {
<?xml version="1.0" encoding="UTF-8" standalone="no"?>
<svg
   xmlns:svg="http://www.w3.org/2000/svg"
   width="13px"
   height="13px"
   version="1.1"
   id="classicradiobutton">
  <defs
     id="defs4785">
    <linearGradient
       id="linearGradient5440">
      <stop
         style="stop-color:$DARK;stop-opacity:1;"
         offset="0"
         id="stop5436" />
      <stop
         style="stop-color:$BACKGROUND;stop-opacity:0;"
         offset="1"
         id="stop5438" />
    </linearGradient>
    <linearGradient
       xlink:href="#linearGradient5440"
       id="linearGradient5442"
       x1="2"
       y1="2"
       x2="11"
       y2="11"
       gradientUnits="userSpaceOnUse" />
  </defs>
  <g id="layer1" >
    <circle
	style="fill:url(#linearGradient5442);fill-opacity:1;fill-rule:evenodd;stroke-width:1;stroke-miterlimit:4;stroke-dasharray:none"
       id="outer3Drim"
       cx="6.5"
       cy="6.5"
       r="6.5" />
    <circle
       style="fill:$SELECT;fill-opacity:1;fill-rule:evenodd;stroke-width:1;stroke-miterlimit:4;stroke-dasharray:none"
       id="circlebackdrop"
       cx="6.5"
       cy="6.5"
       r="6" />
    <circle
       style="fill:$INDICATOR;fill-opacity:1;fill-rule:evenodd;stroke-width:1;stroke-miterlimit:4;stroke-dasharray:none"
       id="indicatordot"
       cx="6.5"
       cy="6.5"
       r="3.0" />
    </g>
</svg>
}

proc ::tk::icons::createCheckRadioBtns {kind palette} {
    # imgPalette[0], // background
    # imgPalette[1], // light
    # imgPalette[2], // dark
    # imgPalette[3], // select
    # imgPalette[4], // indicator
    # imgPalette[5]);// disable
    set imgPalette [split $palette ,]

    if {$kind eq "checkbox"} {
	if {[info exists ::tk::icons::checkboxImg($palette)]} {
	    return $::tk::icons::checkboxImg($palette)
	}
    } elseif {$kind eq "radiobox"} {
	if {[info exists ::tk::icons::radioboxImg($palette)]} {
	    return $::tk::icons::radiobpxImg($palette)
	}
    }

    lassign $imgPalette background light dark select indicator disable

    set fmt $::tk::svgFmt

    # Default starting point (on-enabled)
    set BACKGROUND $background ;# A, B, F (see tkUnixButton.c)
    set LIGHT $light           ;# C
    set SELECT $select         ;# D
    set DARK  $dark            ;# E
    set INDICATOR $indicator   ;# G
    set DISABLE $disable       ;# H

    # Generation counter, used to make sure new image names do not
    # collide with existing images already in use, possibly from
    # another palette.
    set gen [incr ::tk::icons::__gen__]

    # Create image for each unique state
    foreach disable {enabled disabled} {
	foreach on {off on} {
	    switch ${on}-${disable} {
		off-disabled {
		    set INDICATOR $disable
		    set BACKGROUND $disable
		    set SELECT $disable
		}
		off-enabled  {
		    set INDICATOR $select
		    set BACKGROUND $background
		    set SELECT $select
		}
		on-disabled  {
		    set INDICATOR $background
		    set BACKGROUND $disable
		    set SELECT $disable
		}
		on-enabled   {
		    set INDICATOR $indicator
		    set BACKGROUND $background
		    set SELECT $select
		}
	    }
	    set name ::tk::icons::${kind}-${gen}-${on}-${disable}
	    if {$kind eq "checkbox"} {
		set svg [subst -nocommands -nobackslash $::tk::icons::checkbox]
	    } else {
		set svg [subst -nocommands -nobackslash $::tk::icons::radiobox]
	    }
	    set img [image create photo $name \
			 -format $fmt -data $svg]
	    if {$kind eq "checkbox"} {
		lappend ::tk::icons::checkboxImg($palette) $img
	    } else {
		lappend ::tk::icons::radioboxImg($palette) $img
	    }
	}
    }
    return [set ::tk::icons::${kind}Img($palette)]
}
