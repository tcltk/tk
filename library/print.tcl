# print.tcl --

# This file defines the 'tk print' command for printing of the canvas
# widget and text on X11, Windows, and macOS. It implements an abstraction
# layer that presents a consistent API across the three platforms.

# Copyright © 2009 Michael I. Schwartz.
# Copyright © 2021 Kevin Walzer/WordTech Communications LLC.
# Copyright © 2021 Harald Oehlmann, Elmicron GmbH
# Copyright © 2022 Emiliano Gavilan
#
# See the file "license.terms" for information on usage and redistribution
# of this file, and for a DISCLAIMER OF ALL WARRANTIES.

namespace eval ::tk::print {
    namespace import -force ::tk::msgcat::*

    # makeTempFile:
    #    Create a temporary file and populate its contents
    # Arguments:
    #	 filename - base of the name of the file to create
    #    contents - what to put in the file; defaults to empty
    # Returns:
    #    Full filename for created file
    #
    proc makeTempFile {filename {contents ""}} {
	set dumpfile [file join /tmp rawprint.txt]
	set tmpfile [file join /tmp $filename]
	set f [open $dumpfile w]
	try {
	    puts -nonewline $f $contents
	} finally {
	    close $f
	    if {[file extension $filename] == ".ps"} {
		#don't apply formatting to PostScript
		file rename -force $dumpfile $tmpfile
	    } else {
	    #Make text fixed width for improved printed output
		exec fmt -w 75 $dumpfile > $tmpfile
	    }
	    return $tmpfile
	}
    }

    if {[tk windowingsystem] eq "win32"} {
	variable printer_name
	variable copies
	variable dpi_x
	variable dpi_y
	variable paper_width
	variable paper_height
	variable margin_left
	variable margin_top
	variable printargs
	array set printargs {}

	# Multiple utility procedures for printing text based on the
	# C printer primitives.

	# _set_dc:
	# Select printer and set device context and other parameters
	# for print job.
	#
	proc _set_dc {} {
	    variable printargs
	    variable printer_name
	    variable paper_width
	    variable paper_height
	    variable dpi_x
	    variable dpi_y
	    variable copies

	    #First, we select the printer.
	    _selectprinter

	    #Next, set values. Some are taken from the printer,
	    #some are sane defaults.

	    if {[info exists printer_name] && $printer_name ne ""} {
		set printargs(hDC) $printer_name
		set printargs(pw) $paper_width
		set printargs(pl) $paper_height
		set printargs(lm) 1000
		set printargs(tm) 1000
		set printargs(rm) 1000
		set printargs(bm) 1000
		set printargs(resx) $dpi_x
		set printargs(resy) $dpi_y
		set printargs(copies) $copies
		set printargs(resolution) [list $dpi_x $dpi_y]
	    }
	}

	# _print_data
	# This function prints multiple-page files, using a line-oriented
	# function, taking advantage of knowing the character widths.
	# Arguments:
	# data -       Text data for printing
	# breaklines - If non-zero, keep newlines in the string as
	#              newlines in the output.
	# font -       Font for printing
	proc _print_data {data {breaklines 1} {font ""}} {
	    variable printargs
	    variable printer_name

	    _set_dc

	    if {![info exists printer_name]} {
		return
	    }

	    if {$font eq ""} {
		_gdi characters $printargs(hDC) -array printcharwid
	    } else {
		_gdi characters $printargs(hDC) -font $font -array printcharwid
	    }
	    set pagewid [expr {($printargs(pw) - $printargs(rm) ) / 1000 * $printargs(resx)}]
	    set pagehgt [expr {($printargs(pl) - $printargs(bm) ) / 1000 * $printargs(resy)}]
	    set totallen [string length $data]
	    set curlen 0
	    set curhgt [expr {$printargs(tm) * $printargs(resy) / 1000}]

	    _opendoc
	    _openpage

	    while {$curlen < $totallen} {
		set linestring [string range $data $curlen end]
		if {$breaklines} {
		    set endind [string first "\n" $linestring]
		    if {$endind >= 0} {
			set linestring [string range $linestring 0 $endind]
			# handle blank lines....
			if {$linestring eq ""} {
			    set linestring " "
			}
		    }
		}

		set result [_print_page_nextline $linestring \
				printcharwid printargs $curhgt $font]
		incr curlen [lindex $result 0]
		incr curhgt [lindex $result 1]
		if {$curhgt + [lindex $result 1] > $pagehgt} {
		    _closepage
		    _openpage
		    set curhgt [expr {$printargs(tm) * $printargs(resy) / 1000}]
		}
	    }

	    _closepage
	    _closedoc
	}

	# _print_file
	# This function prints multiple-page files
	# It will either break lines or just let them run over the
	# margins (and thus truncate).
	# The font argument is JUST the font name, not any additional
	# arguments.
	# Arguments:
	#   filename -   File to open for printing
	#   breaklines - 1 to break lines as done on input, 0 to ignore newlines
	#   font -       Optional arguments to supply to the text command
	proc _print_file {filename {breaklines 1} {font ""}} {
	    set fn [open $filename r]
	    set data [read $fn]
	    close $fn
	    _print_data $data $breaklines $font
	}

	# _print_page_nextline
	# Returns the pair "chars y"
	# where chars is the number of characters printed on the line
	# and y is the height of the line printed
	# Arguments:
	#   string -         Data to print
	#   pdata -         Array of values for printer characteristics
	#   cdata -         Array of values for character widths
	#   y -              Y value to begin printing at
	#   font -           if non-empty specifies a font to draw the line in
	proc _print_page_nextline {string carray parray y font} {
	    upvar #0 $carray charwidths
	    upvar #0 $parray printargs

	    variable printargs

	    set endindex 0
	    set totwidth 0
	    set maxwidth [expr {
		(($printargs(pw) - $printargs(rm)) / 1000) * $printargs(resx)
	    }]
	    set maxstring [string length $string]
	    set lm [expr {$printargs(lm) * $printargs(resx) / 1000}]

	    for {set i 0} {($i < $maxstring) && ($totwidth < $maxwidth)} {incr i} {
		incr totwidth $charwidths([string index $string $i])
		# set width($i) $totwidth
	    }

	    set endindex $i
	    set startindex $endindex

	    if {$i < $maxstring} {
		# In this case, the whole data string is not used up, and we
		# wish to break on a word. Since we have all the partial
		# widths calculated, this should be easy.

		set endindex [expr {[string wordstart $string $endindex] - 1}]
		set startindex [expr {$endindex + 1}]

		# If the line is just too long (no word breaks), print as much
		# as you can....
		if {$endindex <= 1} {
		    set endindex $i
		    set startindex $i
		}
	    }

	    set txt [string trim [string range $string 0 $endindex] "\r\n"]
	    if {$font ne ""} {
		set result [_gdi text $printargs(hDC) $lm $y \
				 -anchor nw -justify left \
				 -text $txt -font $font]
	    } else {
		set result [_gdi text $printargs(hDC) $lm $y \
				 -anchor nw -justify left -text $txt]
	    }
	    return "$startindex $result"
	}

	# These procedures read in the canvas widget, and write all of
	# its contents out to the Windows printer.

	variable option
	variable vtgPrint

	proc _init_print_canvas {} {
	    variable option
	    variable vtgPrint
	    variable printargs

	    set vtgPrint(printer.bg) white
	}

	proc _is_win {} {
	    variable printargs

	    return [info exist tk_patchLevel]
	}

	# _print_widget
	# Main procedure for printing a widget.  Currently supports
	# canvas widgets.  Handles opening and closing of printer.
	# Arguments:
	#   wid -              The widget to be printed.
	#   printer -          Flag whether to use the default printer.
	#   name  -            App name to pass to printer.

	proc _print_widget {wid {printer default} {name "Tk Print Output"}} {
	    variable printargs
	    variable printer_name

	    _set_dc

	    if {![info exists printer_name]} {
		return
	    }

	    _opendoc
	    _openpage

	    # Here is where any scaling/gdi mapping should take place
	    # For now, scale so the dimensions of the window are sized to the
	    # width of the page. Scale evenly.

	    # For normal windows, this may be fine--but for a canvas, one
	    # wants the canvas dimensions, and not the WINDOW dimensions.
	    if {[winfo class $wid] eq "Canvas"} {
		set sc [$wid cget -scrollregion]
		# if there is no scrollregion, use width and height.
		if {$sc eq ""} {
		    set window_x [winfo pixels $wid [$wid cget -width]]
		    set window_y [winfo pixels $wid [$wid cget -height]]
		} else {
		    set window_x [lindex $sc 2]
		    set window_y [lindex $sc 3]
		}
	    } else {
		set window_x [winfo width $wid]
		set window_y [winfo height $wid]
	    }

	    set printer_x [expr {
		( $printargs(pw) - $printargs(lm) - $printargs(rm) ) *
		$printargs(resx)  / 1000.0
	    }]
	    set printer_y [expr {
		( $printargs(pl) - $printargs(tm) - $printargs(bm) ) *
		$printargs(resy) / 1000.0
	    }]
	    set factor_x [expr {$window_x / $printer_x}]
	    set factor_y [expr {$window_y / $printer_y}]

	    if {$factor_x < $factor_y} {
		set lo $window_y
		set ph $printer_y
	    } else {
		set lo $window_x
		set ph $printer_x
	    }

	    _gdi map $printargs(hDC) -logical $lo -physical $ph \
		-offset $printargs(resolution)

	    # Handling of canvas widgets.
	    switch [winfo class $wid] {
		Canvas {
		    _print_canvas $printargs(hDC) $wid
		}
		default {
		    puts "Can't print items of type [winfo class $wid]. No handler registered"
		}
	    }

	    # End printing process.
	    _closepage
	    _closedoc
	}

	#  _print_canvas
	# Main procedure for writing canvas widget items to printer.
	# Arguments:
	#    hdc -              The printer handle.
	#    cw  -              The canvas widget.
	proc _print_canvas {hdc cw} {
	    variable  vtgPrint
	    variable printargs

	    # Get information about page being printed to
	    # print_canvas.CalcSizing $cw
	    set vtgPrint(canvas.bg) [string tolower [$cw cget -background]]

	    # Re-write each widget from cw to printer
	    foreach id [$cw find all] {
		set type [$cw type $id]
		if {[info commands _print_canvas.$type] eq "_print_canvas.$type"} {
		    _print_canvas.[$cw type $id] $printargs(hDC) $cw $id
		} else {
		    puts "Omitting canvas item of type $type since there is no handler registered for it"
		}
	    }
	}

	# These procedures support the various canvas item types, reading the
	# information about the item on the real canvas and then writing a
	# similar item to the printer.

	# _print_canvas.line
	# Description:
	#   Prints a line item.
	# Arguments:
	#   hdc -              The printer handle.
	#   cw  -              The canvas widget.
	#   id  -              The id of the canvas item.
	proc _print_canvas.line {hdc cw id} {
	    variable vtgPrint
	    variable printargs

	    set color [_print_canvas.TransColor [$cw itemcget $id -fill]]
	    if {[string match $vtgPrint(printer.bg) $color]} {
		return
	    }

	    set coords  [$cw coords $id]
	    set wdth    [$cw itemcget $id -width]
	    set arrow   [$cw itemcget $id -arrow]
	    set arwshp  [$cw itemcget $id -arrowshape]
	    set dash    [$cw itemcget $id -dash]
	    set smooth  [$cw itemcget $id -smooth]
	    set splinesteps [$cw itemcget $id -splinesteps]

	    set cmdargs {}

	    if {$wdth > 1} {
		lappend cmdargs -width $wdth
	    }
	    if {$dash ne ""} {
		lappend cmdargs -dash $dash
	    }
	    if {$smooth ne ""} {
		lappend cmdargs -smooth $smooth
	    }
	    if {$splinesteps ne ""} {
		lappend cmdargs -splinesteps $splinesteps
	    }

	    set result [_gdi line $hdc {*}$coords \
			    -fill $color -arrow $arrow -arrowshape $arwshp \
			    {*}$cmdargs]
	    if {$result ne ""} {
		puts $result
	    }
	}

	# _print_canvas.arc
	#   Prints a arc item.
	# Args:
	#   hdc -              The printer handle.
	#   cw  -              The canvas widget.
	#   id  -              The id of the canvas item.
	proc _print_canvas.arc {hdc cw id} {
	    variable vtgPrint
	    variable printargs

	    set color [_print_canvas.TransColor [$cw itemcget $id -outline]]
	    if {[string match $vtgPrint(printer.bg) $color]} {
		return
	    }
	    set coords  [$cw coords $id]
	    set wdth    [$cw itemcget $id -width]
	    set style   [$cw itemcget $id -style]
	    set start   [$cw itemcget $id -start]
	    set extent  [$cw itemcget $id -extent]
	    set fill    [$cw itemcget $id -fill]

	    set cmdargs {}
	    if {$wdth > 1} {
		lappend cmdargs -width $wdth
	    }
	    if {$fill ne ""} {
		lappend cmdargs -fill $fill
	    }

	    _gdi arc $hdc {*}$coords \
		-outline $color -style $style -start $start -extent $extent \
		{*}$cmdargs
	}

	# _print_canvas.polygon
	#   Prints a polygon item.
	# Arguments:
	#   hdc -              The printer handle.
	#   cw  -              The canvas widget.
	#   id  -              The id of the canvas item.
	proc _print_canvas.polygon {hdc cw id} {
	    variable vtgPrint
	    variable printargs

	    set fcolor [_print_canvas.TransColor [$cw itemcget $id -fill]]
	    if {$fcolor eq ""} {
		set fcolor $vtgPrint(printer.bg)
	    }
	    set ocolor [_print_canvas.TransColor [$cw itemcget $id -outline]]
	    if {$ocolor eq ""} {
		set ocolor $vtgPrint(printer.bg)
	    }
	    set coords  [$cw coords $id]
	    set wdth [$cw itemcget $id -width]
	    set smooth  [$cw itemcget $id -smooth]
	    set splinesteps [$cw itemcget $id -splinesteps]

	    set cmdargs {}
	    if {$smooth ne ""} {
		lappend cmdargs -smooth $smooth
	    }
	    if {$splinesteps ne ""} {
		lappend cmdargs -splinesteps $splinesteps
	    }

	    _gdi polygon $hdc {*}$coords \
		-width $wdth -fill $fcolor -outline $ocolor {*}$cmdargs
	}

	# _print_canvas.oval
	#   Prints an oval item.
	# Arguments:
	#   hdc -              The printer handle.
	#   cw  -              The canvas widget.
	#   id  -              The id of the canvas item.
	proc _print_canvas.oval {hdc cw id} {
	    variable vtgPrint

	    set fcolor [_print_canvas.TransColor [$cw itemcget $id -fill]]
	    if {$fcolor eq ""} {
		set fcolor $vtgPrint(printer.bg)
	    }
	    set ocolor [_print_canvas.TransColor [$cw itemcget $id -outline]]
	    if {$ocolor eq ""} {
		set ocolor $vtgPrint(printer.bg)
	    }
	    set coords  [$cw coords $id]
	    set wdth [$cw itemcget $id -width]

	    _gdi oval $hdc {*}$coords \
		-width $wdth -fill $fcolor -outline $ocolor
	}

	# _print_canvas.rectangle
	#   Prints a rectangle item.
	# Arguments:
	#   hdc -              The printer handle.
	#   cw  -              The canvas widget.
	#   id  -              The id of the canvas item.
	proc _print_canvas.rectangle {hdc cw id} {
	    variable vtgPrint

	    set fcolor [_print_canvas.TransColor [$cw itemcget $id -fill]]
	    if {$fcolor eq ""} {
		set fcolor $vtgPrint(printer.bg)
	    }
	    set ocolor [_print_canvas.TransColor [$cw itemcget $id -outline]]
	    if {$ocolor eq ""} {
		set ocolor $vtgPrint(printer.bg)
	    }
	    set coords  [$cw coords $id]
	    set wdth [$cw itemcget $id -width]

	    _gdi rectangle $hdc {*}$coords \
		-width $wdth -fill $fcolor -outline $ocolor
	}

	# _print_canvas.text
	#   Prints a text item.
	# Arguments:
	#   hdc -              The printer handle.
	#   cw  -              The canvas widget.
	#   id  -              The id of the canvas item.
	proc _print_canvas.text {hdc cw id} {
	    variable vtgPrint
	    variable printargs

	    set color [_print_canvas.TransColor [$cw itemcget $id -fill]]
	    #    if {"white" eq [string tolower $color]} {return}
	    #    set color black
	    set txt [$cw itemcget $id -text]
	    if {$txt eq ""} {
		return
	    }
	    set coords [$cw coords $id]
	    set anchr [$cw itemcget $id -anchor]

	    set bbox [$cw bbox $id]
	    set wdth [expr {[lindex $bbox 2] - [lindex $bbox 0]}]

	    set just [$cw itemcget $id -justify]

	    # Get the real canvas font info and create a compatible font,
	    # suitable for printer name extraction.
	    set font [font create {*}[font actual [$cw itemcget $id -font]]]

	    # Just get the name and family, or some of the _gdi commands will
	    # fail.
	    set font [list [font configure $font -family] \
			  -[font configure $font -size]]

	    _gdi text $hdc {*}$coords \
		-fill $color -text $txt -font $font \
		-anchor $anchr -width $wdth -justify $just
	}

	# _print_canvas.image
	# Prints an image item.
	# Arguments:
	#   hdc -              The printer handle.
	#   cw  -              The canvas widget.
	#   id  -              The id of the canvas item.
	proc _print_canvas.image {hdc cw id} {
	    # First, we have to get the image name.
	    set imagename [$cw itemcget $id -image]

	    # Now we get the size.
	    set wid [image width $imagename]
	    set hgt [image height $imagename]

	    # Next, we get the location and anchor
	    set anchor [$cw itemcget $id -anchor]
	    set coords [$cw coords $id]

	    _gdi photo $hdc -destination $coords -photo $imagename
	}

	# _print_canvas.bitmap
	#   Prints a bitmap item.
	# Arguments:
	#   hdc -              The printer handle.
	#   cw  -              The canvas widget.
	#   id  -              The id of the canvas item.
	proc _print_canvas.bitmap {hdc cw id} {
	    variable option
	    variable vtgPrint

	    # First, we have to get the bitmap name.
	    set imagename [$cw itemcget $id -image]

	    # Now we get the size.
	    set wid [image width $imagename]
	    set hgt [image height $imagename]

	    #Next, we get the location and anchor.
	    set anchor [$cw itemcget $id -anchor]
	    set coords [$cw coords $id]

	    # Since the GDI commands don't yet support images and bitmaps,
	    # and since this represents a rendered bitmap, we CAN use
	    # copybits IF we create a new temporary toplevel to hold the beast.
	    # If this is too ugly, change the option!

	    if {[info exist option(use_copybits)]} {
		set firstcase $option(use_copybits)
	    } else {
		set firstcase 0
	    }
	    if {$firstcase > 0} {
		set tl [toplevel .tmptop[expr {int( rand() * 65535 )}] \
			    -height $hgt -width $wid \
			    -background $vtgPrint(canvas.bg)]
		canvas $tl.canvas -width $wid -height $hgt
		$tl.canvas create image 0 0 -image $imagename -anchor nw
		pack $tl.canvas -side left -expand false -fill none
		tkwait visibility $tl.canvas
		update
		set srccoords [list 0 0 [expr {$wid - 1}] [expr {$hgt - 1}]]
		set dstcoords [list [lindex $coords 0] [lindex $coords 1] [expr {$wid - 1}] [expr {$hgt - 1}]]
		_gdi copybits $hdc -window $tl -client \
		    -source $srccoords -destination $dstcoords
		destroy $tl
	    } else {
		_gdi bitmap $hdc {*}$coords \
		    -anchor $anchor -bitmap $imagename
	    }
	}

	# These procedures transform attribute setting from the real
	# canvas to the appropriate setting for printing to paper.

	# _print_canvas.TransColor
	#   Does the actual transformation of colors from the
	#   canvas widget to paper.
	# Arguments:
	#   color -            The color value to be transformed.
	proc _print_canvas.TransColor {color} {
	    variable vtgPrint
	    variable printargs

	    switch [string toupper $color] {
		$vtgPrint(canvas.bg)       {return $vtgPrint(printer.bg)}
	    }
	    return $color
	}

	# Initialize all the variables once.
	_init_print_canvas
    }
    #end win32 procedures
}

# Begin X11 procedures. They depends on Cups being installed.
# X11 procedures abstracts print management with a "cups" ensemble command

#	cups defaultprinter	returns the default printer
#	cups getprinters	returns a dictionary of printers along
#				with printer info
#	cups print $printer $data ?$options?
#				print the data (binary) on a given printer
#				with the provided (supported) options:
#				-colormode -copies -format -margins
#				-media -nup -orientation
#				-prettyprint -title -tzoom

# Some output configuration that on other platforms is managed through
# the printer driver/dialog is configured through the canvas postscript command.
if {[tk windowingsystem] eq "x11"} {
    if {[info commands ::tk::print::cups] eq ""} {
	namespace eval ::tk::print::cups {
	    # Pure Tcl cups ensemble command implementation
	    variable pcache
	}

	proc ::tk::print::cups::defaultprinter {} {
	    set default {}
	    regexp {: ([^[:space:]]+)$} [exec lpstat -d] _ default
	    return $default
	}

	proc ::tk::print::cups::getprinters {} {
	    variable pcache
	    # Test for existence of lpstat command to obtain the list of
	    # printers.
	    # Return an error if not found.
	    set res {}
	    try {
		set printers [lsort -unique [split [exec lpstat -e] \n]]
		foreach printer $printers {
		    set options [Parseoptions [exec lpoptions -p $printer]]
		    dict set res $printer $options
		}
	    } trap {POSIX ENOENT} {e o} {
		# no such command in PATH
		set cmd [lindex [dict get $o -errorstack ] 1 2]
		return -code error "Unable to obtain the list of printers.\
		    Command \"$cmd\" not found.\
		    Please install the CUPS package for your system."
	    } trap {CHILDSTATUS} {} {
		# command returns a non-0 exit status. Wrong print system?
		set cmd [lindex [dict get $o -errorstack ] 1 2]
		return -code error "Command \"$cmd\" return with errors"
	    }
	    return [set pcache $res]
	}

	# Parseoptions
	#   Parse lpoptions -d output. It has three forms
	#     option-key
	#     option-key=option-value
	#     option-key='option value with spaces'
	# Arguments:
	#   data - data to process.
	#
	proc ::tk::print::cups::Parseoptions {data} {
	    set res {}
	    set re {[^ =]+|[^ ]+='[^']+'|[^ ]+=[^ ']+}
	    foreach tok [regexp -inline -all $re $data] {
		lassign [split $tok "="] k v
		dict set res $k [string trim $v "'"]
	    }
	    return $res
	}

	proc ::tk::print::cups::print {printer data args} {
	    variable pcache
	    if {$printer ni [dict keys $pcache]} {
		return -code error "unknown printer or class \"$printer\""
	    }
	    set title "Tk print job"
	    set options {
		-colormode -copies -format -margins -media -nup -orientation
		-prettyprint -title -tzoom
	    }
	    while {[llength $args]} {
		set opt [tcl::prefix match $options [lpop args 0]]
		switch  $opt {
		    -colormode {
			set opts {auto monochrome color}
			set val [tcl::prefix match $opts [lpop args 0]]
			lappend printargs -o print-color-mode=$val
		    }
		    -copies {
			set val [lpop args 0]
			if {![string is integer -strict $val] ||
			    $val < 0 || $val > 100
			} {
			    # save paper !!
			    return -code error "copies must be an integer\
				between 0 and 100"
			}
			lappend printargs -o copies=$val
		    }
		    -format {
			set opts {auto pdf postscript text}
			set val [tcl::prefix match $opts [lpop args 0]]
			# lpr uses auto always
		    }
		    -margins {
			set val [lpop args 0]
			if {[llength $val] != 4 ||
			    ![string is integer -strict [lindex $val 0]] ||
			    ![string is integer -strict [lindex $val 1]] ||
			    ![string is integer -strict [lindex $val 2]] ||
			    ![string is integer -strict [lindex $val 3]]
			} {
			    return -code error "margins must be a list of 4\
				integers: top left bottom right"
			}
			lappend printargs -o page-top=[lindex $val 0]
			lappend printargs -o page-left=[lindex $val 1]
			lappend printargs -o page-bottom=[lindex $val 2]
			lappend printargs -o page-right=[lindex $val 3]
		    }
		    -media {
			set opts {a4 legal letter}
			set val [tcl::prefix match $opts [lpop args 0]]
			lappend printargs -o media=$val
		    }
		    -nup {
			set val [lpop args 0]
			if {$val ni {1 2 4 6 9 16}} {
			    return -code error "number-up must be 1, 2, 4, 6, 9 or\
				16"
			}
			lappend printargs -o number-up=$val
		    }
		    -orientation {
			set opts {portrait landscape}
			set val [tcl::prefix match $opts [lpop args 0]]
			if {$val eq "landscape"}
			lappend printargs -o landscape=true
		    }
		    -prettyprint {
			lappend printargs -o prettyprint=true
			# prettyprint mess with these default values if set
			# so we force them.
			# these will be overriden if set after this point
			if {[lsearch $printargs {cpi=*}] == -1} {
			    lappend printargs -o cpi=10.0
			    lappend printargs -o lpi=6.0
			}
		    }
		    -title {
			set title [lpop args 0]
		    }
		    -tzoom {
			set val [lpop args 0]
			if {![string is double -strict $val] ||
			    $val < 0.5 || $val > 2.0
			 } {
			    return -code error "text zoom must be a number between\
				0.5 and 2.0"
			}
			# CUPS text filter defaults to lpi=6 and cpi=10
			lappend printargs -o cpi=[expr {10.0 / $val}]
			lappend printargs -o lpi=[expr {6.0 / $val}]
		    }
		    default {
			# shouldn't happen
		    }
		}
	    }
	    # build our options
	    lappend printargs -T $title
	    lappend printargs -P $printer
	    # open temp file
	    set fd [file tempfile fname tk_print]
	    chan configure $fd -translation binary
	    chan puts $fd $data
	    chan close $fd
	    # add -r to automatically delete temp files
	    exec lpr {*}$printargs -r $fname &
	}

	namespace eval ::tk::print::cups {
	    namespace export defaultprinter getprinters print
	    namespace ensemble create
	}
    };# ::tk::print::cups

    namespace eval ::tk::print {

	variable mcmap
	set mcmap(media) [dict create \
	    [mc "Letter"]	letter \
	    [mc "Legal"]	legal \
	    [mc "A4"]		a4]
	set mcmap(orient) [dict create \
	    [mc "Portrait"]	portrait \
	    [mc "Landscape"]	landscape]
	set mcmap(color) [dict create \
	    [mc "RGB"]		color \
	    [mc "Grayscale"]	gray]

	# available print options
	variable optlist
	set optlist(printer)	{}
	set optlist(media)	[dict keys $mcmap(media)]
	set optlist(orient)	[dict keys $mcmap(orient)]
	set optlist(color)	[dict keys $mcmap(color)]
	set optlist(number-up)	{1 2 4 6 9 16}

	# selected options
	variable option
	set option(printer)	{}
	# Initialize with sane defaults.
	set option(copies)	1
	set option(media)	[mc "A4"]
	# Canvas options
	set option(orient)	[mc "Portrait"]
	set option(color)	[mc "RGB"]
	set option(czoom)	100
	# Text options.
	# See libcupsfilter's cfFilterTextToPDF() and cups-filters's texttopdf
	# known options:
	# prettyprint, wrap, columns, lpi, cpi
	set option(number-up)	1
	set option(tzoom)	100; # we derive lpi and cpi from this value
	set option(pprint)	0  ; # pretty print
	set option(margin-top)	20 ; # ~ 7mm (~ 1/4")
	set option(margin-left)	20 ; # ~ 7mm (~ 1/4")
	set option(margin-right)  20 ; # ~ 7mm (~ 1/4")
	set option(margin-bottom) 20 ; # ~ 7mm (~ 1/4")

	# array to collect printer information
	variable pinfo
	array set pinfo {}

	# a map for printer state -> human readable message
	variable statemap
	dict set statemap 3 [mc "Idle"]
	dict set statemap 4 [mc "Printing"]
	dict set statemap 5 [mc "Printer stopped"]
    }

    # ttk version of [tk_optionMenu]
    # var should be a full qualified varname
    proc ::tk::print::ttk_optionMenu {w var args} {
	ttk::menubutton $w -textvariable $var -menu $w.menu
	menu $w.menu
	foreach option $args {
	    $w.menu add command \
		-label $option \
		-command [list set $var $option]
	}
	# return the same value as tk_optionMenu
	return $w.menu
    }

    # _setprintenv
    #   Set the print environtment - list of printers, state and options.
    # Arguments:
    #   none.
    #
    proc ::tk::print::_setprintenv {} {
	variable option
	variable optlist
	variable pinfo

	set optlist(printer) {}
	dict for {printer options} [cups getprinters] {
	    lappend optlist(printer) $printer
	    set pinfo($printer) $options
	}

	# It's an error to not have any printer configured
	if {[llength $optlist(printer)] == 0} {
	    return -code error "No installed printers found.\
	    Please check or update your CUPS installation."
	}

	# If no printer is selected, check for the default one
	# If none found, use the first one from the list
	if {$option(printer) eq ""} {
	    set option(printer) [cups defaultprinter]
	    if {$option(printer) eq ""} {
		set option(printer) [lindex $optlist(printer) 0]
	    }
	}
    }

    # _print
    #   Main printer dialog.
    #   Select printer, set options, and fire print command.
    # Arguments:
    #   w - widget with contents to print.
    #
    proc ::tk::print::_print {w} {
	variable optlist
	variable option
	variable pinfo
	variable statemap

	# default values for dialog widgets
	option add *Printdialog*TLabel.anchor e
	option add *Printdialog*TMenubutton.Menu.tearOff 0
	option add *Printdialog*TMenubutton.width 12
	option add *Printdialog*TSpinbox.width 12
	# this is tempting to add, but it's better to leave it to
	# user's taste.
	# option add *Printdialog*Menu.background snow

	set class [winfo class $w]
	if {$class ni {Text Canvas}} {
	    return -code error "printing windows of class \"$class\"\
		is not supported"
	}
	# Should this be called with every invocaton?
	# Yes. It allows dynamic discovery of newly added printers
	# whithout having to restart the app
	_setprintenv

	set p ._print
	destroy $p

	# Copy the current values to a dialog's temporary variable.
	# This allow us to cancel the dialog discarding any changes
	# made to the options
	namespace eval dlg {variable option}
	array set dlg::option [array get option]
	set var [namespace which -variable dlg::option]

	# The toplevel of our dialog
	toplevel $p -class Printdialog
	place [ttk::frame $p.background] -x 0 -y 0 -relwidth 1.0 -relheight 1.0
	wm title $p [mc "Print"]
	wm resizable $p 0 0
	wm attributes $p -type dialog
	wm transient $p [winfo toplevel $w]

	# The printer to use
	set pf [ttk::frame $p.printerf]
	pack $pf -side top -fill x -expand no -padx 9p -pady 9p

	ttk::label $pf.printerl -text "[mc "Printer"]"
	set tv [ttk::treeview $pf.prlist -height 5 \
	    -columns {printer location state} \
	    -show headings \
	    -selectmode browse]
	$tv configure \
	    -yscrollcommand [namespace code [list _scroll $pf.sy]] \
	    -xscrollcommand [namespace code [list _scroll $pf.sx]]
	ttk::scrollbar $pf.sy -command [list $tv yview]
	ttk::scrollbar $pf.sx -command [list $tv xview] -orient horizontal
	$tv heading printer  -text [mc "Printer"]
	$tv heading location -text [mc "Location"]
	$tv heading state    -text [mc "State"]
	$tv column printer  -width 200 -stretch 0
	$tv column location -width 100 -stretch 0
	$tv column state    -width 250 -stretch 0

	foreach printer $optlist(printer) {
	    set location [dict getdef $pinfo($printer) printer-location ""]
	    set nstate [dict getdef $pinfo($printer) printer-state 0]
	    set state [dict getdef $statemap $nstate ""]
	    switch -- $nstate {
		3 - 4 {
		    set accepting [dict getdef $pinfo($printer) \
			printer-is-accepting-jobs ""]
		    if {$accepting ne ""} {
			append state ". " [mc "Printer is accepting jobs"]
		    }
		}
		5 {
		    set reason [dict getdef $pinfo($printer) \
			printer-state-reasons ""]
		    if {$reason ne ""} {
			    append state ". (" $reason ")"
		    }
		}
	    }
	    set id [$tv insert {} end \
		-values [list $printer $location $state]]
	    if {$option(printer) eq $printer} {
		$tv selection set $id
	    }
	}

	grid $pf.printerl -sticky w
	grid $pf.prlist $pf.sy -sticky news
	grid $pf.sx -sticky ew
	grid remove $pf.sy $pf.sx
	bind $tv <<TreeviewSelect>> [namespace code {_onselect %W}]

	# Start of printing options
	set of [ttk::labelframe $p.optionsframe -text [mc "Options"]]
	pack $of -fill x -padx 9p -pady {0 9p} -ipadx 2p -ipady 2p

	# COPIES
	ttk::label $of.copiesl -text "[mc "Copies"] :"
	ttk::spinbox $of.copies -textvariable ${var}(copies) \
	    -from 1 -to 1000
	grid $of.copiesl $of.copies -sticky ew -padx 2p -pady 2p
	$of.copies state readonly

	# PAPER SIZE
	ttk::label $of.medial -text "[mc "Paper"] :"
	ttk_optionMenu $of.media ${var}(media) {*}$optlist(media)
	grid $of.medial $of.media -sticky ew -padx 2p -pady 2p

	if {$class eq "Canvas"} {
	    # additional options for Canvas output
	    # SCALE
	    ttk::label $of.percentl -text "[mc "Scale"] :"
	    ttk::spinbox $of.percent -textvariable ${var}(czoom) \
		-from 5 -to 500 -increment 5
	    grid $of.percentl $of.percent -sticky ew -padx 2p -pady 2p
	    $of.percent state readonly

	    # ORIENT
	    ttk::label $of.orientl -text "[mc "Orientation"] :"
	    ttk_optionMenu $of.orient ${var}(orient) {*}$optlist(orient)
	    grid $of.orientl $of.orient -sticky ew -padx 2p -pady 2p

	    # COLOR
	    ttk::label $of.colorl -text "[mc "Output"] :"
	    ttk_optionMenu $of.color ${var}(color) {*}$optlist(color)
	    grid $of.colorl $of.color -sticky ew -padx 2p -pady 2p
	} elseif {$class eq "Text"} {
	    # additional options for Text output
	    # NUMBER-UP
	    ttk::label $of.nupl -text "[mc "Pages per sheet"] :"
	    ttk_optionMenu $of.nup ${var}(number-up) {*}$optlist(number-up)
	    grid $of.nupl $of.nup -sticky ew -padx 2p -pady 2p

	    # TEXT SCALE
	    ttk::label $of.tzooml -text "[mc "Text scale"] :"
	    ttk::spinbox $of.tzoom -textvariable ${var}(tzoom) \
		-from 50 -to 200 -increment 5
	    grid $of.tzooml $of.tzoom -sticky ew -padx 2p -pady 2p
	    $of.tzoom state readonly

	    # PRETTY PRINT (banner on top)
	    ttk::checkbutton $of.pprint -onvalue 1 -offvalue 0 \
		-text [mc "Pretty print"] \
		-variable ${var}(pprint)
	    grid $of.pprint - -sticky ew -padx 2p -pady 2p
	}

	# The buttons frame.
	set bf [ttk::frame $p.buttonf]
	pack $bf -fill x -expand no -side bottom -padx 9p -pady {0 9p}

	ttk::button $bf.print -text [mc "Print"] \
	    -command [namespace code [list _runprint $w $class $p]]
	ttk::button $bf.cancel -text [mc "Cancel"] \
	    -command [list destroy $p]
	pack $bf.print  -side right
	pack $bf.cancel -side right -padx {0 4.5p}

	# cleanup binding
	bind $bf <Destroy> [namespace code [list _cleanup $p]]

	# Center the window as a dialog.
	::tk::PlaceWindow $p
    }

    # _onselect
    #   Updates the selected printer when treeview selection changes.
    # Arguments:
    #   tv - treeview pathname.
    #
    proc ::tk::print::_onselect {tv} {
	variable dlg::option
	set id [$tv selection]
	if {$id eq ""} {
	    # is this even possible?
	    set option(printer) ""
	} else {
	    set option(printer) [$tv set $id printer]
	}
    }

    # _scroll
    #   Implements autoscroll for the printers view
    #
    proc ::tk::print::_scroll {sbar from to} {
	if {$from == 0.0 && $to == 1.0} {
	    grid remove $sbar
	} else {
	    grid $sbar
	    $sbar set $from $to
	}
    }

    # _cleanup
    #   Perform cleanup when the dialog is destroyed.
    # Arguments:
    #   p - print dialog pathname (not used).
    #
    proc ::tk::print::_cleanup {p} {
	namespace delete dlg
    }

    # _runprint -
    #   Execute the print command--print the file.
    # Arguments:
    #   w     - widget with contents to print.
    #   class - class of the widget to print (Canvas or Text).
    #   p     - print dialog pathname.
    #
    proc ::tk::print::_runprint {w class p} {
	variable option
	variable mcmap

	# copy the values back from the dialog
	array set option [array get dlg::option]

	# get (back) name of media from the translated one
	set media [dict get $mcmap(media) $option(media)]
	set printargs {}
	lappend printargs -title "[tk appname]: Tk window $w"
	lappend printargs -copies $option(copies)
	lappend printargs -media $media

	if {$class eq "Canvas"} {
	    set colormode [dict get $mcmap(color) $option(color)]
	    set rotate 0
	    if {[dict get $mcmap(orient) $option(orient)] eq "landscape"} {
		set rotate 1
	    }
	    # Scale based on size of widget, not size of paper.
	    # TODO: is this correct??
	    set printwidth [expr {
		$option(czoom) / 100.0 * [winfo width $w]
	    }]
	    set data [encoding convertto iso8859-1 [$w postscript \
		-colormode $colormode -rotate $rotate -pagewidth $printwidth]]
	} elseif {$class eq "Text"} {
	    set tzoom [expr {$option(tzoom) / 100.0}]
	    if {$option(tzoom) != 100} {
		lappend printargs -tzoom $tzoom
	    }
	    if {$option(pprint)} {
		lappend printargs -prettyprint
	    }
	    if {$option(number-up) != 1} {
		lappend printargs -nup $option(number-up)
	    }
	    # these are hardcoded. Should we allow the user to control
	    # margins?
	    lappend printargs -margins [list \
		$option(margin-top)    $option(margin-left) \
		$option(margin-bottom) $option(margin-right) ]
	    # get the data in shape. Cupsfilter's text filter wraps lines
	    # at character level, not words, so we do it by ourselves.
	    # compute usable page width in inches
	    set pw [dict get {a4 8.27 legal 8.5 letter 8.5} $media]
	    set pw [expr {
		$pw - ($option(margin-left) + $option(margin-right)) / 72.0
	    }]
	    # set the wrap length at 98% of computed page width in chars
	    # the 9.8 constant is the product 10.0 (default cpi) * 0.95
	    set wl [expr {int( 9.8 * $pw / $tzoom )}]
	    set data [encoding convertto utf-8 [_wrapLines [$w get 1.0 end] $wl]]
	}

	# launch the job in the background
	after idle [namespace code \
	    [list cups print $option(printer) $data {*}$printargs]]
	destroy $p
    }

    # _wrapLines -
    #   wrap long lines into lines of at most length wl at word boundaries
    # Arguments:
    #   str   - string to be wrapped
    #   wl    - wrap length
    #
    proc ::tk::print::_wrapLines {str wl} {
	# This is a really simple algorithm: it breaks a line on space or tab
	# character, collapsing them only at the breaking point.
	# Leading space is left as-is.
	# For a full fledged line breaking algorithm see
	# Unicode® Standard Annex #14 "Unicode Line Breaking Algorithm"
	set res {}
	incr wl -1
	set re [format {((?:^|[^[:blank:]]).{0,%d})(?:[[:blank:]]|$)} $wl]
	foreach line [split $str \n] {
	    lappend res {*}[lmap {_ l} [regexp -all -inline -- $re $line] {
		set l
	    }]
	}
	return [join $res \n]
    }
}
#end X11 procedures

namespace eval ::tk::print {
    #begin macOS Aqua procedures
    if {[tk windowingsystem] eq "aqua"} {
	# makePDF -
	#   Convert a file to PDF
	# Arguments:
	#   inFilename -  file containing the data to convert; format is
	#                 autodetected.
	#   outFilename - base for filename to write to; conventionally should
	#                 have .pdf as suffix
	# Returns:
	#   The full pathname of the generated PDF.
	#
	proc makePDF {inFilename outFilename} {
	    set out [::tk::print::makeTempFile $outFilename]
	    try {
		exec /usr/sbin/cupsfilter $inFilename > $out
	    } trap NONE {msg} {
		# cupsfilter produces a lot of debugging output, which we
		# don't want.
		regsub -all -line {^(?:DEBUG|INFO):.*$} $msg "" msg
		set msg [string trimleft [regsub -all {\n+} $msg "\n"] "\n"]
		if {$msg ne ""} {
		    # Lines should be prefixed with WARN or ERROR now
		    puts $msg
		}
	    }
	    return $out
	}
    }
    #end macOS Aqua procedures

    namespace export canvas text
    namespace ensemble create
}

# tk print --
# This procedure prints the canvas and text widgets using platform-
# native API's.
#   Arguments:
#      w: Widget to print.
proc ::tk::print {w} {
    switch [winfo class $w],[tk windowingsystem] {
	"Canvas,win32" {
	    tailcall ::tk::print::_print_widget $w 0 "Tk Print Output"
	}
	"Canvas,x11" {
	    tailcall ::tk::print::_print $w
	}
	"Canvas,aqua" {
	    ::tk::print::_printcanvas $w
	    set printfile /tmp/tk_canvas.pdf
	    ::tk::print::_print $printfile
	}
	"Text,win32" {
	    tailcall ::tk::print::_print_data [$w get 1.0 end] 1 {Arial 12}
	}
	"Text,x11" {
	    tailcall ::tk::print::_print $w
	}
	"Text,aqua" {
	    set txtfile [::tk::print::makeTempFile tk_text.txt [$w get 1.0 end]]
	    try {
		set printfile [::tk::print::makePDF $txtfile [file join /tmp tk_text.pdf]]
		::tk::print::_print $printfile
	    } finally {
		file delete $txtfile
	    }
	}

	default {
	    return -code error -errorcode {TK PRINT CLASS_UNSUPPORTED} \
		"widgets of class [winfo class $w] are not supported on\
		this platform"
	}
    }
}

#Add this command to the tk command ensemble: tk print
#Thanks to Christian Gollwitzer for the guidance here
namespace ensemble configure tk -map \
    [dict merge [namespace ensemble configure tk -map] \
	 {print ::tk::print}]

return

# Local Variables:
# mode: tcl
# fill-column: 78
# End:
