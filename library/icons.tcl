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
