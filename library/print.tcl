# print.tcl --

# This file defines the 'tk print' command for printing of the canvas widget and text on X11, Windows, and macOS. It implements an abstraction layer that
# presents a consistent API across the three platforms.

# Copyright © 2009 Michael I. Schwartz.
# Copyright © 2021 Kevin Walzer/WordTech Communications LLC.
# Copyright © 2021 Harald Oehlmann, Elmicron GmbH
#
# See the file "license.terms" for information on usage and redistribution
# of this file, and for a DISCLAIMER OF ALL WARRANTIES.


namespace eval ::tk::print {


    if {[tk windowingsystem] eq "win32"} {

	variable ::tk::print::printer_name
	variable ::tk::print::copies
	variable ::tk::print::dpi_x
	variable ::tk::print::dpi_y
	variable ::tk::print::paper_width
	variable ::tk::print::paper_height
	variable ::tk::print::margin_left
	variable ::tk::print::margin_top
	variable printargs
	array set printargs {}

	# Multiple utility procedures for printing text based on the C printer
	# primitives. 

	# _page_args: 
	# Parse common arguments for text processing in the other commands.
	#  
	# 
	proc _page_args {} {
	    variable printargs

	    #First, we select the printer.
	    ::tk::print::_selectprinter

	    if {$::tk::print::printer_name == ""} {
		#they pressed cancel
		return
	    }

	    #Next, set values.
	    set printargs(hDC) [list $::tk::print::printer_name]
	    set printargs(pw) $::tk::print::paper_width
	    set printargs(pl) $::tk::print::paper_height
	    set printargs(lm) 100 
	    set printargs(tm) 100 
	    set printargs(rm) [expr $printargs(pw) - $printargs(lm)]
	    set printargs(bm) [expr $printargs(pl) - $printargs(tm)]
	    set printargs(resx) $::tk::print::dpi_x
	    set printargs(resy) $::tk::print::dpi_y
	    set printargs(copies) $::tk::print::copies
		
		parray printargs
	    
	    return printargs
	}

	# _ print_page_data
	# This proc is the simplest way to print a small amount of
	# text on a page. The text is formatted in a box the size of the
	# selected page and margins.
	#
	# Arguments:
	# data  -        Text data for printing
	# fontargs -    Optional arguments to supply to the text command

	proc _print_page_data { data {fontargs {}} } {

	    variable printargs

	    _page_args 
		
		array get printargs
		
		puts "_print_page_data"
	    
	    set tm [ expr $printargs(tm) * $printargs(resy) / 1000 ]
	    set lm [ expr $printargs(lm) * $printargs(resx) / 1000 ]
	    set pw [ expr ($printargs(pw)  - $printargs(rm))  / 1000 * $printargs(resx) ]
	    ::tk::print::_opendoc
	    ::tk::print::_openpage
	    eval ::tk::print::_gdi text $printargs(hDC) $lm $tm \
		-anchor nw -text [list $data] \
		-width $pw \
		$fontargs
	    ::tk::print::_closepage
	    ::tk::print::_closedoc
	}


	# _print_page_file
	# This is the simplest way to print a small file
	# on a page. The text is formatted in a box the size of the
	# selected page and margins.
	# Arguments:
	# data     -      Text data for printing
	# fontargs -      Optional arguments to supply to the text command

	proc _print_page_file { filename {fontargs {}} } {
	    
	    variable printargs
	    array get printargs
	    
	    set fn [open $filename r]

	    set data [ read $fn ]

	    close $fn

	    _print_page_data $data $fontargs
	}


	# _print_data
	# This function prints multiple-page files, using a line-oriented
	# function, taking advantage of knowing the character widths.
	# Arguments: 
	# data -	     Text data for printing
	# breaklines - If non-zero, keep newlines in the string as
	#              newlines in the output.
	# font -       Font for printing

	proc _print_data { data {breaklines 1 } {font {}} } {
	    
		variable printargs

	    _page_args
	    
	    array get printargs
		
		puts "_print_data"
	    
	    if { [string length $font] == 0 } {
		eval ::tk::print::_gdi characters  $printargs(hDC) -array printcharwid
	    } else {
		eval ::tk::print::_gdi characters $printargs(hDC) -font $font -array printcharwid
	    }

	    set pagewid  [ expr ( $printargs(pw) - $printargs(rm) ) / 1000 * $printargs(resx) ]
	    set pagehgt  [ expr ( $printargs(pl) - $printargs(bm) ) / 1000 * $printargs(resy) ]
	    set totallen [ string length $data ]
	    set curlen 0
	    set curhgt [ expr $printargs(tm) * $printargs(resy) / 1000 ]

	    ::tk::print::_opendoc
	    ::tk::print::_openpage
	    while { $curlen < $totallen } {
		set linestring [ string range $data $curlen end ]
		if { $breaklines } {
		    set endind [ string first "\n" $linestring ]
		    if { $endind != -1 } {
			set linestring [ string range $linestring 0 $endind ] 
			# handle blank lines....
			if { $linestring == "" } { 
			    set linestring " " 
			}
		    } 
		} 
		
		set plist [array get printargs]
		set clist [array get printcharwid]
		
		puts "plist is $plist"
		puts "clist is $clist"

		set result [_print_page_nextline $linestring \
				$clist $plist $curhgt $font]
		incr curlen [lindex $result 0]
		incr curhgt [lindex $result 1]
		if { [expr $curhgt + [lindex $result 1] ] > $pagehgt } {
		    ::tk::print::_closepage
		    ::tk::print::_openpage
		    set curhgt [ expr $printargs(tm) * $printargs(resy) / 1000 ]
		}
	    }
	    ::tk::print::_print_closepage
	    ::tk::print::_print_closedoc
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
	
	proc _print_file { filename {breaklines 1 } { font {}} } {
	    
	    variable printargs
	    
	    array get printargs
	    
	    set fn [open $filename r]

	    set data [ read $fn ]

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

	proc _print_page_nextline { string clist plist y font } {
	
	   
		array set charwidths $clist
		array set printargs $plist
		
		puts "_print_page_nextline"
		
	    set endindex 0
	    set totwidth 0
	    set maxwidth [ expr ( ( $printargs(pw) - $printargs(rm) ) / 1000 ) * $printargs(resx) ]
	    set maxstring [ string length $string ]
	    set lm [ expr $printargs(lm) * $printargs(resx) / 1000 ]

	    for { set i 0 } { ( $i < $maxstring ) && ( $totwidth < $maxwidth ) } { incr i } {
		set ch [ string index $string $i ]
		if [ info exist charwidths($ch) ] {
		    incr totwidth $charwidths([string index $string $i])
		} else {
		    incr totwidth $charwidths(n)
		}
		# set width($i) $totwidth
	    }

	    set endindex $i
	    set startindex $endindex

	    if { $i < $maxstring } {
		# In this case, the whole data string is not used up, and we wish to break on a 
		# word. Since we have all the partial widths calculated, this should be easy.
		set endindex [ expr [string wordstart $string $endindex] - 1 ]
		set startindex [ expr $endindex + 1 ]

		# If the line is just too long (no word breaks), print as much as you can....
		if { $endindex <= 1 } {
		    set endindex $i
		    set startindex $i
		}
	    }

	    if { [string length $font] > 0 } {
		set result [ ::tk::print::_gdi text $printargs(hDC) $lm $y \
				 -anchor nw -justify left \
				 -text [ string trim [ string range $string 0 $endindex ] "\r\n" ] \
				 -font $font ]
	    } else {
		set result [ ::tk::print::_gdi text $printargs(hDC) $lm $y \
				 -anchor nw -justify left \
				 -text [string trim [ string range $string 0 $endindex ] "\r\n" ] ]
	    }

	    return "$startindex $result"
	}


	
	# These procedures read in the canvas widget, and write all of 
	# its contents out to the Windows printer.      
	
	variable option
	variable vtgPrint

	proc _init_print_canvas { } {
	    variable option
	    variable vtgPrint
	    variable printargs
	    
	    array get printargs

	    set option(use_copybits) 1
	    set vtgPrint(printer.bg) white
	}

	proc _is_win {} {
	    variable printargs
	    
	    array get printargs
	    
	    return [ info exist tk_patchLevel ]
	}

	
	# _print_widget
	# Main procedure for printing a widget.  Currently supports
	# canvas widgets.  Handles opening and closing of printer.
	# Arguments:
	#   wid -              The widget to be printed. 
	#   printer -          Flag whether to use the default printer. 
	#   name  -            App name to pass to printer.

	proc _print_widget { wid {printer default} {name "Tk Print Job"} } {
	    
	    variable printargs
	    
	    _page_args 
	    
	    array get printargs

	    ::tk::print::_opendoc
	    ::tk::print::_openpage

	    # Here is where any scaling/gdi mapping should take place
	    # For now, scale so the dimensions of the window are sized to the
	    # width of the page. Scale evenly.

	    # For normal windows, this may be fine--but for a canvas, one wants the 
	    # canvas dimensions, and not the WINDOW dimensions.
	    if { [winfo class $wid] == "Canvas" } {
		set sc [ lindex [ $wid configure -scrollregion ] 4 ]
		# if there is no scrollregion, use width and height.
		if { "$sc" == "" } {
		    set window_x [ lindex [ $wid configure -width ] 4 ]
		    set window_y [ lindex [ $wid configure -height ] 4 ]
		} else {
		    set window_x [ lindex $sc 2 ]
		    set window_y [ lindex $sc 3 ]
		}
	    } else {
		set window_x [ winfo width $wid ]
		set window_y [ winfo height $wid ]
	    }

	    set pd "page dimensions"
	    set pm "page margins"
	    set ppi "pixels per inch"
	    
	    set printer_x [ expr ( [lindex $p($pd) 0] - \
				       [lindex $p($pm) 0 ] - \
				       [lindex $p($pm) 2 ] \
				       ) * \
				[lindex $p($ppi) 0] / 1000.0 ]
	    set printer_y [ expr ( [lindex $p($pd) 1] - \
				       [lindex $p($pm) 1 ] - \
				       [lindex $p($pm) 3 ] \
				       ) * \
				[lindex $p($ppi) 1] / 1000.0 ]
	    set factor_x [ expr $window_x / $printer_x ]
	    set factor_y [ expr $window_y / $printer_y ]
	    
	    if { $factor_x < $factor_y } {
		set lo $window_y
		set ph $printer_y
	    } else {
		set lo $window_x
		set ph $printer_x
	    }

	    ::tk::print::_gdi map $printargs(hDC) -logical $lo -physical $ph -offset $p(resolution)
	    
	    # handling of canvas widgets
	    # additional procs can be added for other widget types
	    switch [winfo class $wid] {
		Canvas {
		    #	    if {[catch {
		    _print_canvas [lindex $printargs(hDC) 0] $wid
		    #	    } msg]} {
		    #		debug_puts "print_widget: $msg"
		    #		error "Windows Printing Problem: $msg"
		    #	    }
		}
		default {
		    puts "Can't print items of type [winfo class $wid]. No handler registered"
		}
	    }

	    # end printing process ------
	    ::tk::print::_closepage
	    ::tk::print::_closedoc
	    ::tk::print::_closeprinter
	}


	
	#  _print_canvas
	# Main procedure for writing canvas widget items to printer.
	# Arguments: 
	#    hdc -              The printer handle.
	#    cw  -              The canvas widget.
	

	proc _print_canvas {hdc cw} {
	    variable  vtgPrint
	    
	    variable printargs
	    array get printargs

	    # get information about page being printed to
	    # print_canvas.CalcSizing $cw
	    set vtgPrint(canvas.bg) [string tolower [$cw cget -background]]

	    # re-write each widget from cw to printer
	    foreach id [$cw find all] {
		set type [$cw type $id]
		if { [ info commands _print_canvas.$type ] == "_print_canvas.$type" } {
		    _print_canvas.[$cw type $id] $printargs(hDC) $cw $id
		} else {
		    puts "Omitting canvas item of type $type since there is no handler registered for it"
		}
	    }
	}
	
	# These procedures support the various canvas item types,     
	# reading the information about the item on the real canvas   
	# and then writing a similar item to the printer.           
	
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
	    array get printargs

	    set color [_print_canvas.TransColor [$cw itemcget $id -fill]]
	    if {[string match $vtgPrint(printer.bg) $color]} {return}
	    
	    set coords  [$cw coords $id]
	    set wdth    [$cw itemcget $id -width]
	    set arrow   [$cw itemcget $id -arrow]
	    set arwshp  [$cw itemcget $id -arrowshape]
	    set dash    [$cw itemcget $id -dash]
	    set smooth  [$cw itemcget $id -smooth ]
	    set splinesteps [ $cw itemcget $id -splinesteps ]
	    
	    set cmmd  "::tk::print::_gdi line $printargs(hDC) $coords -fill $color -arrow $arrow -arrowshape [list $arwshp]"
	    
	    if { $wdth > 1 } {
		set cmmd "$cmmd -width $wdth"
	    }
	    
	    if { $dash != "" } {
		set cmmd "$cmmd -dash [list $dash]"
	    }
	    
	    if { $smooth != "" } {
		set cmmd "$cmmd -smooth $smooth"
	    }
	    
	    if { $splinesteps != "" } {
		set cmmd "$cmmd -splinesteps $splinesteps"
	    }
	    
	    set result [eval $cmmd]
	    if { $result != "" } {
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
	    array get printargs

	    set color [print_canvas.TransColor [$cw itemcget $id -outline]]
	    if { [string match $vtgPrint(printer.bg) $color] } {
		return
	    }
	    set coords  [$cw coords $id]
	    set wdth    [$cw itemcget $id -width]
	    set style   [ $cw itemcget $id -style ]
	    set start   [ $cw itemcget $id -start ]
	    set extent  [ $cw itemcget $id -extent ]
	    set fill    [ $cw itemcget $id -fill ]
	    
	    set cmmd  "::tk::print::_gdi arc $printargs(hDC) $coords -outline $color -style $style -start $start -extent $extent"
	    if { $wdth > 1 } {
		set cmmd "$cmmd -width $wdth"
	    }
	    if { $fill != "" } {
		set cmmd "$cmmd -fill $fill"
	    }
	    
	    eval $cmmd
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
	    array get printargs

	    set fcolor [_print_canvas.TransColor [$cw itemcget $id -fill]]
	    if { ![string length $fcolor] } {
		set fcolor $vtgPrint(printer.bg)
	    }
	    set ocolor [_print_canvas.TransColor [$cw itemcget $id -outline]]
	    if { ![string length $ocolor] } {
		set ocolor $vtgPrint(printer.bg)
	    }
	    set coords  [$cw coords $id]
	    set wdth [$cw itemcget $id -width]
	    set smooth  [$cw itemcget $id -smooth ]
	    set splinesteps [ $cw itemcget $id -splinesteps ]
	    

	    set cmmd "::tk::print::_gdi polygon $printargs(hDC) $coords -width $wdth \
		-fill $fcolor -outline $ocolor"
	    if { $smooth != "" } {
		set cmmd "$cmmd -smooth $smooth"
	    }
	    
	    if { $splinesteps != "" } {
		set cmmd "$cmmd -splinesteps $splinesteps"
	    }
	    
	    eval $cmmd
	}


	
	# _print_canvas.oval
	#   Prints an oval item.
	# Arguments:
	#   hdc -              The printer handle.
	#   cw  -              The canvas widget.
	#   id  -              The id of the canvas item.
	
	proc _print_canvas.oval { hdc cw id } {
	    variable vtgPrint
	    
	    variable printargs
	    array get printargs

	    set fcolor [_print_canvas.TransColor [$cw itemcget $id -fill]]
	    if {![string length $fcolor]} {set fcolor $vtgPrint(printer.bg)}
	    set ocolor [print_canvas.TransColor [$cw itemcget $id -outline]]
	    if {![string length $ocolor]} {set ocolor $vtgPrint(printer.bg)}
	    set coords  [$cw coords $id]
	    set wdth [$cw itemcget $id -width]

	    set cmmd "::tk::print::_gdi oval $printargs(hDC) $coords -width $wdth \
		-fill $fcolor -outline $ocolor"

	    eval $cmmd
	}

	
	# _print_canvas.rectangle
	#   Prints a rectangle item.
	# Arguments:
	#   hdc -              The printer handle.
	#   cw  -              The canvas widget.
	#   id  -              The id of the canvas item.
	

	proc _print_canvas.rectangle {hdc cw id} {
	    variable vtgPrint
	    
	    variable printargs
	    array get printargs

	    set fcolor [_print_canvas.TransColor [$cw itemcget $id -fill]]
	    if {![string length $fcolor]} {set fcolor $vtgPrint(printer.bg)}
	    set ocolor [print_canvas.TransColor [$cw itemcget $id -outline]]
	    if {![string length $ocolor]} {set ocolor $vtgPrint(printer.bg)}
	    set coords  [$cw coords $id]
	    set wdth [$cw itemcget $id -width]

	    set cmmd "::tk::print::_gdi rectangle $printargs(hDC) $coords -width $wdth \
		-fill $fcolor -outline $ocolor"

	    eval $cmmd
	}
	
	# _print_canvas.text
	#   Prints a text item.
	# Arguments:
	#   hdc -              The printer handle.
	#   cw  -              The canvas widget.
	#   id  -              The id of the canvas item.
	

	proc _print_canvas.text {hdc cw id} {
	    variable vtgPrint
	    
	    _page_args
	    
	    variable printargs
	    array get printargs
	    
	    set color [_print_canvas.TransColor [$cw itemcget $id -fill]]
	    #    if {[string match white [string tolower $color]]} {return}
	    #    set color black
	    set txt [$cw itemcget $id -text]
	    if {![string length $txt]} {return}
	    set coords [$cw coords $id]
	    set anchr [$cw itemcget $id -anchor]
	    
	    set bbox [$cw bbox $id]
	    set wdth [expr [lindex $bbox 2] - [lindex $bbox 0]]

	    set just [$cw itemcget $id -justify]
	    
	    # Get the canvas font info
	    set font [ $cw itemcget $id -font ]
	    # Find the real font info
	    set font [font actual $font]
	    # Create a compatible font, suitable for printer name extraction
	    set font [ eval font create $font ]
	    # Just get the name and family, or some of the ::tk::print::_gdi commands will fail.
	    # Improve this as GDI improves
	    set font [list [font configure $font -family]  -[font configure $font -size] ]

	    set cmmd "::tk::print::_gdi text $printargs(hDC) $coords -fill $color -text [list $txt] \
		-anchor $anchr -font [ list $font ] \
		-width $wdth -justify $just"
	    eval $cmmd
	} 


	# _print_canvas.image
	# Prints an image item.
	# Arguments:
	#   hdc -              The printer handle.
	#   cw  -              The canvas widget.
	#   id  -              The id of the canvas item.
	

	proc _print_canvas.image {hdc cw id} {

	    variable vtgPrint
	    variable option
	    
	    variable printargs
	    array get printargs

	    # First, we have to get the image name
	    set imagename [ $cw itemcget $id -image]
	    # Now we get the size
	    set wid [ image width $imagename]
	    set hgt [ image height $imagename ]
	    # next, we get the location and anchor
	    set anchor [ $cw itemcget $id -anchor ]
	    set coords [ $cw coords $id ]
	    

	    # Since the GDI commands don't yet support images and bitmaps,
	    # and since this represents a rendered bitmap, we CAN use
	    # copybits IF we create a new temporary toplevel to hold the beast.
	    # if this is too ugly, change the option!
	    if { [ info exist option(use_copybits) ] } {
		set firstcase $option(use_copybits)
	    } else {
		set firstcase 0
	    }

	    if { $firstcase > 0 } {
		set tl [toplevel .tmptop[expr int( rand() * 65535 ) ] -height $hgt -width $wid -background $vtgPrint(printer.bg) ]
		canvas $tl.canvas -width $wid -height $hgt
		$tl.canvas create image 0 0 -image $imagename -anchor nw
		pack $tl.canvas -side left -expand false -fill none 
		tkwait visibility $tl.canvas
		update
		#set srccoords [list "0 0 [ expr $wid - 1] [expr  $hgt - 1 ]" ]
		#set dstcoords [ list "[lindex $coords 0] [lindex $coords 1] [expr $wid - 1] [expr $hgt - 1]" ]
		set srccoords  [ list "0 0 $wid $hgt" ]
		set dstcoords [ list "[lindex $coords 0] [lindex $coords 1] $wid $hgt" ]
		set cmmd "::tk::print::_gdi copybits $printargs(hDC) -window $tl -client -source $srccoords -destination $dstcoords "
		eval $cmmd
		destroy $tl      
	    } else {
		set cmmd "::tk::print::_gdi image $printargs(hDC) $coords -anchor $anchor -image $imagename "
		eval $cmmd
	    }
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
	    
	    variable printargs
	    array get printargs

	    # First, we have to get the bitmap name
	    set imagename [ $cw itemcget $id -image]
	    # Now we get the size
	    set wid [ image width $imagename]
	    set hgt [ image height $imagename ]
	    # next, we get the location and anchor
	    set anchor [ $cw itemcget $id -anchor ]
	    set coords [ $cw coords $id ]
	    
	    # Since the GDI commands don't yet support images and bitmaps,
	    # and since this represents a rendered bitmap, we CAN use
	    # copybits IF we create a new temporary toplevel to hold the beast.
	    # if this is too ugly, change the option!
	    if { [ info exist option(use_copybits) ] } {
		set firstcase $option(use_copybits)
	    } else {
		set firstcase 0
	    }
	    if { $firstcase > 0 } {
		set tl [toplevel .tmptop[expr int( rand() * 65535 ) ] -height $hgt -width $wid -background $vtgPrint(canvas.bg) ]
		canvas $tl.canvas -width $wid -height $hgt
		$tl.canvas create image 0 0 -image $imagename -anchor nw
		pack $tl.canvas -side left -expand false -fill none 
		tkwait visibility $tl.canvas
		update
		set srccoords [list "0 0 [ expr $wid - 1] [expr  $hgt - 1 ]" ]
		set dstcoords [ list "[lindex $coords 0] [lindex $coords 1] [expr $wid - 1] [expr $hgt - 1]" ]
		set cmmd "::tk::print::_gdi copybits $printargs(hDC) -window $tl -client -source $srccoords -destination $dstcoords "
		eval $cmmd
		destroy $tl      
	    } else {
		set cmmd "::tk::print::_gdi bitmap $printargs(hDC) $coords -anchor $anchor -bitmap $imagename"
		eval $cmmd
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
	    array get printargs

	    switch [string toupper $color] {
		$vtgPrint(canvas.bg)       {return $vtgPrint(printer.bg)} 
	    }
	    return $color
	}

	# Initialize all the variables once
	_init_print_canvas
    }
    #end win32 procedures  
}

