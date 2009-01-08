# bgerror.tcl --
#
#	Implementation of the bgerror procedure.  It posts a dialog box with
#	the error message and gives the user a chance to see a more detailed
#	stack trace, and possible do something more interesting with that
#	trace (like save it to a log).  This is adapted from work done by
#	Donal K. Fellows.
#
# Copyright (c) 1998-2000 by Ajuba Solutions.
# Copyright (c) 2007 by ActiveState Software Inc.
# Copyright (c) 2007 Daniel A. Steffen <das@users.sourceforge.net>
# Copyright (c) 2009 Pat Thoyts <patthoyts@users.sourceforge.net>
# 
# RCS: @(#) $Id: bgerror.tcl,v 1.39 2009/01/08 16:31:03 patthoyts Exp $
#

namespace eval ::tk::dialog::error {
    namespace import -force ::tk::msgcat::*
    namespace export bgerror
    option add *ErrorDialog.function.text [mc "Save To Log"] \
	widgetDefault
    option add *ErrorDialog.function.command [namespace code SaveToLog]
    option add *ErrorDialog*Label.font TkCaptionFont widgetDefault
    if {[tk windowingsystem] eq "aqua"} {
	option add *ErrorDialog*background systemAlertBackgroundActive \
		widgetDefault
	option add *ErrorDialog*info.text.background white widgetDefault
	option add *ErrorDialog*Button.highlightBackground \
		systemAlertBackgroundActive widgetDefault
    }
}

image create photo ::tk::dialog::error::image::stop -data {
    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
    ecBf1t8Bael4x3h6yMUAAAAASUVORK5CYII=
}

proc ::tk::dialog::error::Return {} {
    variable button

    .bgerrorDialog.ok configure -state active -relief sunken
    update idletasks
    after 100
    set button 0
}

proc ::tk::dialog::error::Details {} {
    set w .bgerrorDialog
    set caption [option get $w.function text {}]
    set command [option get $w.function command {}]
    if { ($caption eq "") || ($command eq "") } {
	grid forget $w.function
    }
    lappend command [$w.top.info.text get 1.0 end-1c]
    $w.function configure -text $caption -command $command
    grid $w.top.info - -sticky nsew -padx 3m -pady 3m
}

proc ::tk::dialog::error::SaveToLog {text} {
    if { $::tcl_platform(platform) eq "windows" } {
	set allFiles *.*
    } else {
	set allFiles *
    }
    set types [list \
	    [list [mc "Log Files"] .log]      \
	    [list [mc "Text Files"] .txt]     \
	    [list [mc "All Files"] $allFiles] \
	    ]
    set filename [tk_getSaveFile -title [mc "Select Log File"] \
	    -filetypes $types -defaultextension .log -parent .bgerrorDialog]
    if {$filename ne {}} {
        set f [open $filename w]
        puts -nonewline $f $text
        close $f
    }
    return
}

proc ::tk::dialog::error::Destroy {w} {
    if {$w eq ".bgerrorDialog"} {
	variable button
	set button -1
    }
}

# ::tk::dialog::error::bgerror --
#
#	This is the default version of bgerror.
#	It tries to execute tkerror, if that fails it posts a dialog box
#	containing the error message and gives the user a chance to ask
#	to see a stack trace.
#
# Arguments:
#	err - The error message.
#
proc ::tk::dialog::error::bgerror err {
    global errorInfo tcl_platform
    variable button

    set info $errorInfo

    set ret [catch {::tkerror $err} msg];
    if {$ret != 1} {return -code $ret $msg}

    # Ok the application's tkerror either failed or was not found
    # we use the default dialog then :
    set windowingsystem [tk windowingsystem]
    if {$windowingsystem eq "aqua"} {
	set ok [mc Ok]
    } else {
	set ok [mc OK]
    }

    # Truncate the message if it is too wide (>maxLine characters) or
    # too tall (>4 lines).  Truncation occurs at the first point at
    # which one of those conditions is met.
    set displayedErr ""
    set lines 0
    set maxLine 45
    foreach line [split $err \n] {
	if { [string length $line] > $maxLine } {
	    append displayedErr "[string range $line 0 [expr {$maxLine-3}]]..."
	    break
	}
	if { $lines > 4 } {
	    append displayedErr "..."
	    break
	} else {
	    append displayedErr "${line}\n"
	}
	incr lines
    }

    set title [mc "Application Error"]
    set text [mc "Error: %1\$s" $displayedErr]
    set buttons [list ok $ok dismiss [mc "Skip Messages"] \
		     function [mc "Details >>"]]

    # 1. Create the top-level window and divide it into top
    # and bottom parts.

    set dlg .bgerrorDialog
    set bg [ttk::style lookup . -background]
    destroy $dlg
    toplevel $dlg -class ErrorDialog -background $bg
    wm withdraw $dlg
    wm title $dlg $title
    wm iconname $dlg ErrorDialog
    wm protocol $dlg WM_DELETE_WINDOW { }

    if {$windowingsystem eq "aqua"} {
	::tk::unsupported::MacWindowStyle style $dlg moveableAlert {}
    }

    ttk::frame $dlg.bot
    ttk::frame $dlg.top
    if {$windowingsystem eq "x11"} {
	#$dlg.bot configure -relief raised -border 1
	#$dlg.top configure -relief raised -border 1
    }
    pack $dlg.bot -side bottom -fill both
    pack $dlg.top -side top -fill both -expand 1

    set W [ttk::frame $dlg.top.info]
    text $W.text -setgrid true -height 10 -wrap char \
	-yscrollcommand [list $W.scroll set]
    if {$windowingsystem ne "aqua"} {
	$W.text configure -width 40
    }

    ttk::scrollbar $W.scroll -command [list $W.text yview]
    pack $W.scroll -side right -fill y
    pack $W.text -side left -expand yes -fill both
    $W.text insert 0.0 "$err\n$info"
    $W.text mark set insert 0.0
    bind $W.text <ButtonPress-1> { focus %W }
    $W.text configure -state disabled

    # 2. Fill the top part with bitmap and message

    # Max-width of message is the width of the screen...
    set wrapwidth [winfo screenwidth $dlg]
    # ...minus the width of the icon, padding and a fudge factor for
    # the window manager decorations and aesthetics.
    set wrapwidth [expr {$wrapwidth-60-[winfo pixels $dlg 9m]}]
    ttk::label $dlg.msg -justify left -text $text -wraplength $wrapwidth
    ttk::label $dlg.bitmap -image ::tk::dialog::error::image::stop

    grid $dlg.bitmap $dlg.msg -in $dlg.top -row 0 -padx 3m -pady 3m
    grid configure	 $dlg.msg -sticky nsw -padx {0 3m}
    grid rowconfigure	 $dlg.top 1 -weight 1
    grid columnconfigure $dlg.top 1 -weight 1

    # 3. Create a row of buttons at the bottom of the dialog.

    set i 0
    foreach {name caption} $buttons {
	ttk::button $dlg.$name -text $caption -default normal \
	    -command [namespace code [list set button $i]]
	grid $dlg.$name -in $dlg.bot -column $i -row 0 -sticky ew -padx 10
	grid columnconfigure $dlg.bot $i -weight 1
	# We boost the size of some Mac buttons for l&f
	if {$windowingsystem eq "aqua"} {
	    if {($name eq "ok") || ($name eq "dismiss")} {
		grid columnconfigure $dlg.bot $i -minsize 90
	    }
	    grid configure $dlg.$name -pady 7
	}
	incr i
    }
    # The "OK" button is the default for this dialog.
    $dlg.ok configure -default active

    bind $dlg <Return>	[namespace code Return]
    bind $dlg <Destroy>	[namespace code [list Destroy %W]]
    $dlg.function configure -command [namespace code Details]

    # 6. Place the window (centered in the display) and deiconify it.

    ::tk::PlaceWindow $dlg

    # 7. Set a grab and claim the focus too.

    ::tk::SetFocusGrab $dlg $dlg.ok

    # 8. Ensure that we are topmost.

    raise $dlg
    if {$tcl_platform(platform) eq "windows"} {
	# Place it topmost if we aren't at the top of the stacking
	# order to ensure that it's seen
	if {[lindex [wm stackorder .] end] ne "$dlg"} {
	    wm attributes $dlg -topmost 1
        }
    }

    # 9. Wait for the user to respond, then restore the focus and
    # return the index of the selected button.  Restore the focus
    # before deleting the window, since otherwise the window manager
    # may take the focus away so we can't redirect it.  Finally,
    # restore any grab that was in effect.

    vwait [namespace which -variable button]
    set copy $button; # Save a copy...

    ::tk::RestoreFocusGrab $dlg $dlg.ok destroy

    if {$copy == 1} {
	return -code break
    }
}

namespace eval :: {
    # Fool the indexer
    proc bgerror err {}
    rename bgerror {}
    namespace import ::tk::dialog::error::bgerror
}
