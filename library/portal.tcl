# portal.tcl --
#
#	Uses the XDG desktop portal (org.freedesktop.portal.*) on X11 to show
#	the desktop's own file chooser and print dialog and to post
#	notifications.  The D-Bus part is in unix/tkUnixPortal.c.
#
# Copyright © 2026 Serhiy Storchaka
#
# See the file "license.terms" for information on usage and redistribution
# of this file, and for a DISCLAIMER OF ALL WARRANTIES.
#

namespace eval ::tk::portal {
    namespace import -force ::tk::msgcat::*
    # Set to 0 to never use the portal.
    if {![info exists enabled]} {
	variable enabled 1
    }
    # Versions of the portal interfaces, 0 for unavailable ones.
    variable versions {}
    # Directory shown when no -initialdir is given.
    variable lastDir
    # For generating notification ids.
    variable notificationId 0
}

# ::tk::portal::Available --
#
#	Returns true if the portal interface can be used.
#
# Arguments:
#	interface	Name of the interface without "org.freedesktop.portal.".
#	minVersion	Minimal version required.

proc ::tk::portal::Available {interface {minVersion 1}} {
    variable enabled
    variable versions
    if {!$enabled || [info commands _call] eq ""} {
	return 0
    }
    if {![dict exists $versions $interface]} {
	if {[catch {
	    _call org.freedesktop.DBus.Properties Get \
		    [list s org.freedesktop.portal.$interface] {s version}
	} version]} {
	    set version 0
	}
	dict set versions $interface $version
    }
    return [expr {[dict get $versions $interface] >= $minVersion}]
}

# ::tk::portal::Request --
#
#	Calls a portal method that shows a dialog and waits until it is
#	closed.  The application stays responsive, but the toplevel of w does
#	not get pointer events.  The dialog is closed if w is destroyed.
#
# Arguments:
#	w		Window the dialog belongs to.
#	interface	Name of the interface without "org.freedesktop.portal.".
#	method		Method name.
#	args		Arguments after parent_window, as {signature value}.
#			The last one is the options a{sv}.
#
# Results:
#	A dict with keys response (0: success, 1: cancelled, 2: other) and
#	results.

proc ::tk::portal::Request {w interface method args} {
    set top [winfo toplevel $w]
    set busy [expr {[winfo ismapped $top] && ![tk busy status $top]}]
    if {$busy} {
	tk busy hold $top
    }
    try {
	return [_request $w org.freedesktop.portal.$interface $method \
		[list s [_parent $w]] {*}$args]
    } finally {
	if {$busy && [winfo exists $top]} {
	    tk busy forget $top
	}
    }
}

# ::tk::portal::Failed --
#
#	Returns true if a request ended with response 2 (neither success nor
#	cancellation by the user) although its window still exists.  This
#	happens when the portal advertises an interface whose backend does not
#	work.  The interface is then not used again, so that the caller can
#	fall back to the implementation in Tcl.

proc ::tk::portal::Failed {result w interface} {
    variable versions
    if {[dict get $result response] == 2 && [winfo exists $w]} {
	dict set versions $interface 0
	return 1
    }
    return 0
}

# ::tk::portal::PathBytes --
#
#	Returns a file name as a NUL-terminated byte string, as the portal
#	expects it.

proc ::tk::portal::PathBytes {path} {
    return [encoding convertto [encoding system] $path]\0
}

# ::tk::portal::UriToPath --
#
#	Converts a file:// URI to a file name.  Other URIs are returned
#	unchanged.

proc ::tk::portal::UriToPath {uri} {
    if {![regexp {^file://[^/]*(/.*)$} $uri -> path]} {
	return $uri
    }
    set bytes ""
    while {[regexp -indices {%[[:xdigit:]]{2}} $path m]} {
	lassign $m start end
	append bytes [encoding convertto utf-8 \
		[string range $path 0 [expr {$start - 1}]]]
	append bytes [binary format H2 \
		[string range $path [expr {$start + 1}] $end]]
	set path [string range $path [expr {$end + 1}] end]
    }
    append bytes [encoding convertto utf-8 $path]
    return [encoding convertfrom [encoding system] $bytes]
}

# ::tk::portal::Filters --
#
#	Converts the value of the -filetypes option to a list of portal
#	filters {label {{0 pattern} ...}}.  Types with the same label are
#	merged.

proc ::tk::portal::Filters {filetypes} {
    # Only for validating the option.
    ::tk::FDGetFileTypes $filetypes

    set labels {}
    foreach t $filetypes {
	set label [lindex $t 0]
	if {![dict exists $labels $label]} {
	    dict set labels $label {}
	}
	foreach ext [lindex $t 1] {
	    if {$ext eq ""} {
		continue
	    }
	    regsub {^[.]} $ext "*." ext
	    if {[list 0 $ext] ni [dict get $labels $label]} {
		dict lappend labels $label [list 0 $ext]
	    }
	}
    }
    set filters {}
    dict for {label patterns} $labels {
	if {[llength $patterns]} {
	    lappend filters [list $label $patterns]
	}
    }
    return $filters
}

proc ::tk::portal::InitialDir {dir} {
    variable lastDir
    if {$dir ne "" && [file isdirectory $dir]} {
	return [file normalize $dir]
    }
    if {[info exists lastDir] && [file isdirectory $lastDir]} {
	return $lastDir
    }
    return [pwd]
}

proc ::tk::portal::CheckParent {w} {
    if {![winfo exists $w]} {
	return -code error -errorcode [list TK LOOKUP WINDOW $w] \
		"bad window path name \"$w\""
    }
}

# ::tk::portal::FileDialog --
#
#	Implements tk_getOpenFile and tk_getSaveFile.
#
# Arguments:
#	type	"open" or "save".
#	args	Options of tk_getOpenFile or tk_getSaveFile.

proc ::tk::portal::FileDialog {type args} {
    variable lastDir

    set specs {
	{-defaultextension "" "" ""}
	{-filetypes "" "" ""}
	{-initialdir "" "" ""}
	{-initialfile "" "" ""}
	{-parent "" "" "."}
	{-title "" "" ""}
	{-typevariable "" "" ""}
    }
    if {$type eq "open"} {
	lappend specs {-multiple "" "" "0"}
    } else {
	# The portal file choosers always confirm overwriting.
	lappend specs {-confirmoverwrite "" "" "1"}
    }
    tclParseConfigSpec [namespace current]::data $specs "" $args
    variable data
    set opts [array get data]
    unset data
    CheckParent [dict get $opts -parent]
    set filters [Filters [dict get $opts -filetypes]]
    set title [dict get $opts -title]
    if {$title eq ""} {
	set title [expr {$type eq "open" ? [mc "Open"] : [mc "Save As"]}]
    }

    set options [dict create modal {b 1}]
    if {[llength $filters]} {
	dict set options filters [list a(sa(us)) $filters]
	set typevar [dict get $opts -typevariable]
	if {$typevar ne ""} {
	    upvar #0 $typevar typeVariable
	    if {[info exists typeVariable]} {
		set i [lsearch -exact -index 0 $filters $typeVariable]
		if {$i >= 0} {
		    dict set options current_filter \
			    [list (sa(us)) [lindex $filters $i]]
		}
	    }
	}
    }
    set dir [InitialDir [dict get $opts -initialdir]]
    set initialfile [dict get $opts -initialfile]
    if {$type eq "open"} {
	set method OpenFile
	if {[dict get $opts -multiple]} {
	    dict set options multiple {b 1}
	}
	# OpenFile cannot preselect a file, but it can show the directory
	# that contains it (as on macOS).
	if {$initialfile ne ""} {
	    set path [file join $dir $initialfile]
	    if {[file isdirectory $path]} {
		set dir [file normalize $path]
	    } elseif {[file isdirectory [file dirname $path]]} {
		set dir [file normalize [file dirname $path]]
	    }
	}
	dict set options current_folder [list ay [PathBytes $dir]]
    } else {
	set method SaveFile
	set file [file join $dir $initialfile]
	if {$initialfile ne "" && [file isfile $file]} {
	    dict set options current_file \
		    [list ay [PathBytes [file normalize $file]]]
	} else {
	    dict set options current_folder [list ay [PathBytes $dir]]
	    if {$initialfile ne ""} {
		dict set options current_name [list s [file tail $initialfile]]
	    }
	}
    }

    set result [Request [dict get $opts -parent] FileChooser $method \
	    [list s $title] [list a{sv} $options]]
    if {[Failed $result [dict get $opts -parent] FileChooser]} {
	return [::tk::dialog::file:: $type {*}$args]
    }
    set results [dict get $result results]
    if {[dict get $result response] != 0 || ![dict exists $results uris]
	    || ![llength [dict get $results uris]]} {
	return ""
    }
    set files [lmap uri [dict get $results uris] {UriToPath $uri}]
    set lastDir [file dirname [lindex $files 0]]

    set label ""
    if {[dict exists $results current_filter]} {
	set label [lindex [dict get $results current_filter] 0]
    }
    if {$label ne "" && [llength $filters]} {
	set typevar [dict get $opts -typevariable]
	if {$typevar ne ""} {
	    upvar #0 $typevar typeVariable
	    set typeVariable $label
	}
    }

    if {$type eq "open"} {
	if {[dict get $opts -multiple]} {
	    return $files
	}
	return [lindex $files 0]
    }

    # Like tkfbox: if there is no extension, append the default one, or the
    # first extension of the selected filter.
    set file [lindex $files 0]
    set ext [dict get $opts -defaultextension]
    if {$ext eq ""} {
	set i [lsearch -exact -index 0 $filters $label]
	if {$i >= 0} {
	    foreach pattern [lindex $filters $i 1] {
		if {[regexp {^\*(\.\w+)$} [lindex $pattern 1] -> ext]} {
		    break
		}
	    }
	}
    }
    if {$ext ne "" && [file extension $file] eq ""
	    && ![file isdirectory $file]} {
	append file $ext
    }
    return $file
}

# ::tk::portal::ChooseDirectory --
#
#	Implements tk_chooseDirectory.  The portal only lets the user select
#	existing directories, so -mustexist is always satisfied.

proc ::tk::portal::ChooseDirectory {args} {
    variable lastDir

    set specs {
	{-initialdir "" "" ""}
	{-mustexist "" "" 0}
	{-parent "" "" "."}
	{-title "" "" ""}
    }
    tclParseConfigSpec [namespace current]::data $specs "" $args
    variable data
    set opts [array get data]
    unset data
    CheckParent [dict get $opts -parent]
    set title [dict get $opts -title]
    if {$title eq ""} {
	set title [mc "Choose Directory"]
    }
    set dir [InitialDir [dict get $opts -initialdir]]
    set options [dict create modal {b 1} directory {b 1} \
	    current_folder [list ay [PathBytes $dir]]]
    set result [Request [dict get $opts -parent] FileChooser OpenFile \
	    [list s $title] [list a{sv} $options]]
    if {[Failed $result [dict get $opts -parent] FileChooser]} {
	return [::tk::dialog::file::chooseDir:: {*}$args]
    }
    set results [dict get $result results]
    if {[dict get $result response] != 0 || ![dict exists $results uris]
	    || ![llength [dict get $results uris]]} {
	return ""
    }
    set lastDir [UriToPath [lindex [dict get $results uris] 0]]
    return $lastDir
}

# ::tk::portal::Print --
#
#	Implements [tk print] for canvas and text widgets: shows the print
#	dialog of the desktop, then sends the contents of the widget to the
#	selected printer.  Canvases are printed as PostScript, texts are
#	converted to PDF if possible (otherwise sent as plain text).  If the
#	user prints to a file, the document is converted to the requested
#	format (see Convert).
#
# Results:
#	1 if the document was sent, 0 if the dialog was cancelled, -1 if the
#	portal does not work and the caller should fall back.

proc ::tk::portal::Print {w} {
    set class [winfo class $w]
    set title "[tk appname]: Tk window $w"
    set options [dict create modal {b 1}]
    if {[Available Print 3]} {
	# The file formats that can be produced when printing to a file.
	set formats {}
	if {$class eq "Canvas"} {
	    lappend formats ps
	    if {[Converter] ne "" || [auto_execok ps2pdf] ne ""} {
		lappend formats pdf
	    }
	} elseif {[Converter] ne ""} {
	    lappend formats pdf ps
	}
	if {[llength $formats]} {
	    dict set options supported_output_file_formats [list as $formats]
	}
    }
    set result [Request $w Print PreparePrint [list s $title] \
	    {a{sv} {}} {a{sv} {}} [list a{sv} $options]]
    if {[Failed $result $w Print]} {
	return -1
    }
    if {[dict get $result response] != 0} {
	return 0
    }
    set results [dict get $result results]
    set settings {}
    set setup {}
    if {[dict exists $results settings]} {
	set settings [dict get $results settings]
    }
    if {[dict exists $results page-setup]} {
	set setup [dict get $results page-setup]
    }

    # The page size and margins in millimeters.  The default is A4.
    set page [dict create Width 210.0 Height 297.0 \
	    MarginTop 10.0 MarginBottom 10.0 MarginLeft 10.0 MarginRight 10.0]
    dict for {key value} $setup {
	if {[dict exists $page $key] && [string is double -strict $value]} {
	    dict set page $key $value
	}
    }
    dict with page {
	set areaWidth [expr {$Width - $MarginLeft - $MarginRight}]
	set areaHeight [expr {$Height - $MarginTop - $MarginBottom}]
    }
    set orientation portrait
    foreach {dict key} {setup Orientation settings orientation} {
	if {[dict exists [set $dict] $key]} {
	    set orientation [dict get [set $dict] $key]
	}
    }
    set landscape [string match *landscape $orientation]

    # When printing to a file, the dialog writes the document as it is, so
    # it has to be in the requested format already.
    set format ""
    if {[dict exists $settings output-file-format]} {
	set format [string tolower [dict get $settings output-file-format]]
    } elseif {[dict exists $settings output-uri]} {
	set format [string tolower [string trimleft \
		[file extension [dict get $settings output-uri]] .]]
    }
    set formatTypes {pdf application/pdf ps application/postscript}
    set target ""
    if {[dict exists $formatTypes $format]} {
	set target [dict get $formatTypes $format]
    }
    set media ""
    foreach key {PPDName Name} {
	if {[dict exists $setup $key]} {
	    set media [dict get $setup $key]
	    break
	}
    }

    if {$class eq "Canvas"} {
	set data [PrintCanvas $w $settings $page $areaWidth $areaHeight \
		$landscape]
	set type application/postscript
    } else {
	# Not all desktops pass the orientation to the print system, so a text
	# is converted here if possible; otherwise it is printed in portrait.
	if {[Converter] eq ""} {
	    set landscape 0
	} elseif {$target eq ""} {
	    set target application/pdf
	}
	set data [PrintText $w $page $landscape]
	set type text/plain
    }
    if {$target ne "" && $target ne $type} {
	set data [Convert $data $type $target $media $landscape]
    }

    # Pass the document as a file descriptor of an already deleted file.
    set f [file tempfile path tkprint]
    try {
	fconfigure $f -translation binary
	puts -nonewline $f $data
	close $f
	set f [open $path rb]
    } finally {
	file delete $path
    }
    try {
	set options [dict create modal {b 1}]
	if {[dict exists $results token]} {
	    dict set options token [list u [dict get $results token]]
	}
	set result [Request $w Print Print [list s $title] [list h $f] \
		[list a{sv} $options]]
    } finally {
	close $f
    }
    # The document has been handed over to the desktop, which reports print
    # errors itself.  Response 2 is not treated as an error: the KDE backend
    # returns it even after printing successfully.
    return [expr {[dict get $result response] != 1}]
}

# ::tk::portal::Converter --
#
#	Returns the command prefix for converting documents to PDF or
#	PostScript: cupsfilter, with a generic PDF printer description if one
#	is installed, or "" if cupsfilter is not available.

proc ::tk::portal::Converter {} {
    variable converter
    if {![info exists converter]} {
	set converter [auto_execok cupsfilter]
	if {$converter eq "" && [file executable /usr/sbin/cupsfilter]} {
	    set converter /usr/sbin/cupsfilter
	}
	if {$converter ne ""} {
	    foreach ppd {
		/usr/share/ppd/cupsfilters/Generic-PDF_Printer-PDF.ppd
		/usr/share/cups/model/Generic-PDF_Printer-PDF.ppd
	    } {
		if {[file readable $ppd]} {
		    lappend converter -p $ppd
		    break
		}
	    }
	}
    }
    return $converter
}

# ::tk::portal::Convert --
#
#	Converts a document from MIME type "from" to "to" with cupsfilter, or
#	PostScript to PDF with ps2pdf if that fails.
#
# Arguments:
#	data		The document.
#	from, to	MIME types.
#	media		Paper size name (e.g. A4), or "".
#	landscape	Whether plain text is printed in landscape orientation.

proc ::tk::portal::Convert {data from to media landscape} {
    set f [file tempfile path tkprint]
    try {
	fconfigure $f -translation binary
	puts -nonewline $f $data
	close $f
	set error "no converter found"
	set converter [Converter]
	if {$converter ne ""} {
	    # CUPS cannot convert to plain application/postscript.
	    set cmd [list {*}$converter -m [string map {
		application/postscript application/vnd.cups-postscript
	    } $to]]
	    if {$media ne ""} {
		lappend cmd -o media=$media
	    }
	    if {$landscape && $from eq "text/plain"} {
		lappend cmd -o landscape
	    }
	    if {![catch {ReadPipe [list {*}$cmd $path]} result]} {
		return $result
	    }
	    set error $result
	}
	if {$from eq "application/postscript" && $to eq "application/pdf"
		&& [auto_execok ps2pdf] ne ""} {
	    set cmd [list {*}[auto_execok ps2pdf]]
	    if {$media ne ""} {
		lappend cmd -sPAPERSIZE=[string tolower $media]
	    }
	    if {![catch {ReadPipe [list {*}$cmd $path -]} result]} {
		return $result
	    }
	    set error $result
	}
	return -code error -errorcode {TK PRINT CONVERT} \
		"cannot convert the document to $to: $error"
    } finally {
	file delete $path
    }
}

# ::tk::portal::ReadPipe --
#
#	Runs a command and returns its binary output.  Raises an error if the
#	command fails.

proc ::tk::portal::ReadPipe {cmd} {
    set chan [open |[list {*}$cmd 2>/dev/null] rb]
    set data [read $chan]
    close $chan
    if {$data eq ""} {
	return -code error "no output"
    }
    return $data
}

# ::tk::portal::PrintCanvas --
#
#	Returns the PostScript of a canvas.  It is centered on the page and
#	scaled down if it does not fit the printable area.

proc ::tk::portal::PrintCanvas {w settings page areaWidth areaHeight landscape} {
    set args {}
    set sr [$w cget -scrollregion]
    if {$sr ne ""} {
	lassign [lmap x $sr {winfo pixels $w $x}] x1 y1 x2 y2
	lappend args -x $x1 -y $y1 -width [expr {$x2 - $x1}] \
		-height [expr {$y2 - $y1}]
	set width [expr {$x2 - $x1}]
	set height [expr {$y2 - $y1}]
    } else {
	set width [winfo width $w]
	set height [winfo height $w]
    }
    if {$landscape} {
	lassign [list $areaWidth $areaHeight] areaHeight areaWidth
	lappend args -rotate 1
    }
    # The size of the canvas on paper at its natural scale.
    set mmPerPixel [expr {25.4 / [winfo fpixels $w 1i]}]
    if {$width * $mmPerPixel > $areaWidth
	    || $height * $mmPerPixel > $areaHeight} {
	if {$width * $areaHeight > $height * $areaWidth} {
	    lappend args -pagewidth ${areaWidth}m
	} else {
	    lappend args -pageheight ${areaHeight}m
	}
    }
    set colormode color
    if {[dict exists $settings use-color]
	    && [string is false -strict [dict get $settings use-color]]} {
	set colormode gray
    }
    dict with page {
	set x [expr {$MarginLeft + ($Width - $MarginLeft - $MarginRight) / 2}]
	set y [expr {$MarginBottom + ($Height - $MarginTop - $MarginBottom) / 2}]
    }
    return [encoding convertto iso8859-1 [$w postscript \
	    -colormode $colormode -pageanchor center \
	    -pagex ${x}m -pagey ${y}m {*}$args]]
}

# ::tk::portal::PrintText --
#
#	Returns the contents of a text widget as UTF-8, with lines wrapped to
#	fit the page at 10 characters per inch.  The text filter of the print
#	system ignores the margins selected in the dialog and keeps at least
#	1/4 inch at the sides and 1/2 inch at the top and bottom (the printable
#	area of the printer), so longer lines would be broken in mid-word.

proc ::tk::portal::PrintText {w page landscape} {
    dict with page {
	if {$landscape} {
	    set width [expr {$Height - max($MarginTop, 12.7)
		    - max($MarginBottom, 12.7)}]
	} else {
	    set width [expr {$Width - max($MarginLeft, 6.35)
		    - max($MarginRight, 6.35)}]
	}
    }
    set wl [expr {max(10, int($width / 25.4 * 10 + 1e-6))}]
    return [encoding convertto utf-8 \
	    [join [::tk::print::_wrapLines [$w get 1.0 end] $wl] "\n"]]
}

# ::tk::portal::Notify --
#
#	Implements [tk sysnotify].

proc ::tk::portal::Notify {title message} {
    variable notificationId
    set notification [dict create \
	    title [list s $title] \
	    body [list s $message] \
	    icon {(sv) {themed {as dialog-information}}} \
	    priority {s normal}]
    _call org.freedesktop.portal.Notification AddNotification \
	    [list s tk[pid].[incr notificationId]] [list a{sv} $notification]
    return
}
