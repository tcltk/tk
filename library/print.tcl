# print.tcl --

# This file defines the 'tk print' command for printing of the canvas
# widget and text on X11, Windows, and macOS. It implements an abstraction
# layer that presents a consistent API across the three platforms.

# Copyright © 2009 Michael I. Schwartz.
# Copyright © 2021 Kevin Walzer/WordTech Communications LLC.
# Copyright © 2021 Harald Oehlmann, Elmicron GmbH
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
	set f [file tempfile filename $filename]
	try {
	    puts -nonewline $f $contents
	    return $filename
	} finally {
	    close $f
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
		    set window_x [$wid cget -width]
		    set window_y [$wid cget -height]
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

    #begin X11 procedures

    # X11 procedures wrap standard Unix shell commands such as lp/lpr and
    # lpstat for printing. Some output configuration that on other platforms
    # is managed through the printer driver/dialog is configured through the
    # canvas postscript command.

    if {[tk windowingsystem] eq "x11"} {
	variable printcmd ""
	variable printlist {}
	variable choosepaper
	variable chooseprinter {}
	variable p

	# _setprintenv
	#  Set the print environtment - print command, and list of printers.
	#  Arguments:
	#    none.

	proc _setprintenv {} {
	    variable printcmd
	    variable printlist

	    #Test for existence of lpstat command to obtain list of printers. Return error
	    #if not found.

	    catch {exec lpstat -a} msg
	    set notfound "command not found"
	    if {[string first $notfound $msg] >= 0} {
		error "Unable to obtain list of printers. Please install the CUPS package \
		for your system."
		return
	    }
	    set notfound "No destinations added"
	    if {[string first $notfound $msg] >= 0} {
		error "Please check or update your CUPS installation."
		return
	    }

	    # Select print command. We prefer lpr, but will fall back to lp if
	    # necessary.
	    if {[auto_execok lpr] ne ""} {
		set printcmd lpr
	    } else {
		set printcmd lp
	    }

	    #Build list of printers.
	    set printdata [exec lpstat -a]
	    foreach item [split $printdata \n] {
		lappend printlist [lindex [split $item] 0]
	    }
	}

	# _print
	#  Main printer dialog. Select printer, set options, and
	#  fire print command.
	# Arguments:
	#  w - widget with contents to print.
	#

	proc _print {w} {
	    variable printlist
	    variable printcmd
	    variable chooseprinter
	    variable printcopies
	    variable printorientation
	    variable choosepaper
	    variable color
	    variable p
	    variable zoomnumber

	    _setprintenv

	    set chooseprinter [lindex $printlist 0]

	    set p ._print
	    catch {destroy $p}

	    toplevel $p
	    wm title $p "Print"
	    wm resizable $p 0 0

	    frame $p.frame -padx 10 -pady 10
	    pack $p.frame -fill x -expand no

	    #The main dialog
	    frame $p.frame.printframe -padx 5 -pady 5
	    pack $p.frame.printframe -side top -fill x -expand no

	    label $p.frame.printframe.printlabel -text [mc "Printer"]
	    ttk::combobox $p.frame.printframe.mb \
		-textvariable [namespace which -variable chooseprinter] \
		-state readonly -values [lsort -unique $printlist]
	    pack $p.frame.printframe.printlabel $p.frame.printframe.mb \
		-side left -fill x -expand no

	    bind $p.frame.printframe.mb <<ComboboxSelected>>  {
		set chooseprinter {$p.frame.printframe.mb get}
	    }

	    set paperlist [list [mc Letter] [mc Legal] [mc A4]]
	    set colorlist [list [mc Grayscale] [mc RGB]]

	    #Initialize with sane defaults.
	    set printcopies 1
	    set choosepaper [mc A4]
	    set color [mc RGB]
	    set printorientation portrait

	    set percentlist {100 90 80 70 60 50 40 30 20 10}

	    #Base widgets to load.
	    labelframe $p.frame.copyframe -text [mc "Options"] -padx 5 -pady 5
	    pack $p.frame.copyframe -fill x -expand no

	    frame $p.frame.copyframe.l -padx 5 -pady 5
	    pack $p.frame.copyframe.l -side top -fill x -expand no

	    label $p.frame.copyframe.l.copylabel -text [mc "Copies"]
	    spinbox $p.frame.copyframe.l.field -from 1 -to 1000 \
		-textvariable [namespace which -variable printcopies] -width 5

	    pack  $p.frame.copyframe.l.copylabel $p.frame.copyframe.l.field \
		-side left -fill x -expand  no

	    set printcopies [$p.frame.copyframe.l.field get]

	    frame $p.frame.copyframe.r -padx 5 -pady 5
	    pack $p.frame.copyframe.r -fill x -expand no

	    label $p.frame.copyframe.r.paper -text [mc "Paper"]
	    tk_optionMenu $p.frame.copyframe.r.menu \
		[namespace which -variable choosepaper] \
		{*}$paperlist

	    pack $p.frame.copyframe.r.paper $p.frame.copyframe.r.menu \
		-side left -fill x -expand no

	    #Widgets with additional options for canvas output.
	    if {[winfo class $w] eq "Canvas"} {

		frame $p.frame.copyframe.z -padx 5 -pady 5
		pack $p.frame.copyframe.z  -fill x -expand no

		label $p.frame.copyframe.z.zlabel -text [mc "Scale"]
		tk_optionMenu $p.frame.copyframe.z.zentry \
		    [namespace which -variable zoomnumber] \
		    {*}$percentlist

		pack $p.frame.copyframe.z.zlabel $p.frame.copyframe.z.zentry \
		    -side left -fill x -expand no

		frame $p.frame.copyframe.orient -padx 5 -pady 5
		pack $p.frame.copyframe.orient  -fill x -expand no

		label $p.frame.copyframe.orient.text -text [mc "Orientation"]
		radiobutton $p.frame.copyframe.orient.v -text [mc "Portrait"] \
		    -value portrait -compound left \
		    -variable [namespace which -variable printorientation]
		radiobutton $p.frame.copyframe.orient.h -text [mc "Landscape"] \
		    -value landscape -compound left \
		    -variable [namespace which -variable printorientation]

		pack $p.frame.copyframe.orient.text \
		    $p.frame.copyframe.orient.v $p.frame.copyframe.orient.h \
		    -side left -fill x -expand no

		frame $p.frame.copyframe.c -padx 5 -pady 5
		pack $p.frame.copyframe.c  -fill x -expand no

		label $p.frame.copyframe.c.l -text [mc "Output"]
		tk_optionMenu $p.frame.copyframe.c.c \
		    [namespace which -variable color] \
		    {*}$colorlist
		pack $p.frame.copyframe.c.l $p.frame.copyframe.c.c -side left \
		    -fill x -expand no
	    }

	    #Build rest of GUI.
	    frame $p.frame.buttonframe
	    pack $p.frame.buttonframe -fill x -expand no -side bottom

	    button $p.frame.buttonframe.printbutton -text [mc "Print"] \
		-command [namespace code [list _runprint $w]]
	    button $p.frame.buttonframe.cancel -text [mc "Cancel"] \
		-command {destroy ._print}

	    pack $p.frame.buttonframe.printbutton $p.frame.buttonframe.cancel \
		-side right -fill x -expand no
	    #Center the window as a dialog.
	    ::tk::PlaceWindow $p
	}

	# _runprint -
	#   Execute the print command--print the file.
	# Arguments:
	#  w - widget with contents to print.
	#
	proc _runprint {w} {
	    variable printlist
	    variable printcmd
	    variable choosepaper
	    variable chooseprinter
	    variable color
	    variable printcopies
	    variable printorientation
	    variable zoomnumber
	    variable p

	    #First, generate print file.

	    if {[winfo class $w] eq "Text"} {
		set file [makeTempFile tk_text.txt [$w get 1.0 end]]
	    }

	    if {[winfo class $w] eq "Canvas"} {
		if {$color eq [mc "RGB"]} {
		    set colormode color
		} else {
		    set colormode gray
		}

		if {$printorientation eq "landscape"} {
		    set willrotate "1"
		} else {
		    set willrotate "0"
		}

		#Scale based on size of widget, not size of paper.
		set printwidth [expr {$zoomnumber / 100.00 * [winfo width $w]}]
		set file [makeTempFile tk_canvas.ps]
		$w postscript -file $file -colormode $colormode \
		    -rotate $willrotate -pagewidth $printwidth
	    }

	    #Build list of args to pass to print command.

	    set printargs {}
	    set printcopies [$p.frame.copyframe.l.field get]
	    if {$printcmd eq "lpr"} {
		lappend printargs -P $chooseprinter -# $printcopies
	    } else {
		lappend printargs -d $chooseprinter -n $printcopies
	    }

	    after 500
	    exec $printcmd {*}$printargs -o PageSize=$choosepaper $file

	    after 500
	    destroy ._print
	}
    }
    #end X11 procedures

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
	    set psfile [::tk::print::makeTempFile tk_canvas.ps]
	    try {
		$w postscript -file $psfile
		set printfile [::tk::print::makePDF $psfile tk_canvas.pdf]
		::tk::print::_print $printfile
	    } finally {
		file delete $psfile
	    }
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
		set printfile [::tk::print::makePDF $txtfile tk_text.pdf]
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
