# choosedir.tcl --
#
#	Choose directory dialog implementation for Unix/Mac.  Adapted from
#	Christopher Nelson's (chris@pinebush.com) implementation.
#
# Copyright (c) 1998-2000 by Scriptics Corporation.
# All rights reserved.
# 
# RCS: @(#) $Id: choosedir.tcl,v 1.3 2000/02/14 22:00:17 ericm Exp $

# Make sure the tk::dialog namespace, in which all dialogs should live, exists
namespace eval ::tk::dialog {}

# Make the chooseDir namespace inside the dialog namespace
namespace eval ::tk::dialog::chooseDir {
    # value is an array that holds the current selection value for each dialog
    variable value
}

proc ::tk::dialog::chooseDir::tkChooseDirectory { args } {
    variable value

    # Error messages
    append err(usage) "tk_chooseDirectory "
    append err(usage) "?-initialdir directory? ?-mustexist boolean? "
    append err(usage) "?-parent window? ?-title title?"

    set err(wrongNumArgs) "wrong # args: should be \"$err(usage)\""
    set err(valueMissing) "value for \"%s\" missing: should be \"$err(usage)\""
    set err(unknownOpt)   "unknown option \"%s\": should be \"$err(usage)\""
    set err(badWindow)    "bad window path name \"%s\""

    # Default values
    set opts(-initialdir)	[pwd]
    set opts(-mustexist)	0
    set opts(-parent)		.
    set opts(-title)		"Choose Directory"

    # Process args
    set len [llength $args]
    for { set i 0 } { $i < $len } {incr i} {
	set flag [lindex $args $i]
	incr i
	if { $i >= $len } {
	    error [format $err(valueMissing) $flag]
	}
	switch -glob -- $flag {
	    "-initialdir" -
	    "-mustexist"  -
	    "-parent"     -
	    "-title" {
		set opts($flag) [lindex $args $i]
	    }
	    default {
		error [format $err(unknownOpt) $flag]
	    }
	}
    }
	    
    # Handle default parent window
    if { ![winfo exists $opts(-parent)] } {
	error [format $err(badWindow) $opts(-parent)]
    }
    if {[string equal $opts(-parent) "."]} {
	set opts(-parent) ""
    }

    set w [toplevel $opts(-parent).choosedirectory]
    wm title $w $opts(-title)

    # Commands for various bindings (which follow)
    set okCommand  [namespace code \
	    [list Done $w ok $opts(-mustexist)]]

    set cancelCommand  [namespace code \
	    [list Done $w cancel $opts(-mustexist)]]

    # Create controls.
    set lbl  [label $w.l -text "Directory name:" -anchor w]
    set ent  [entry $w.e -width 30]
    set frm  [frame $w.f]
    set lst  [listbox $frm.lb -height 8 \
	    -yscrollcommand [list $frm.sb set] \
	    -selectmode browse \
	    -setgrid true \
	    -exportselection 0 \
	    -takefocus 1]
    set scr  [scrollbar $frm.sb -orient vertical \
	    -command [list $frm.lb yview]]
    set bOK  [button $w.ok     -width 8 -text OK -command $okCommand \
	    -default active]
    set bCan [button $w.cancel -width 8 -text Cancel -command $cancelCommand]

    if {[llength [file volumes]]} {
	# On Macs it would be nice to add a volume combobox
    }

    # Place controls on window
    set padding 4
    grid $lst $scr -sticky nsew
    grid columnconfigure $frm 0 -weight 1
    grid rowconfigure    $frm 0 -weight 1

    grid $lbl $bOK  -padx $padding -pady $padding
    grid $ent $bCan -padx $padding -pady $padding
    grid $frm       -padx $padding -pady $padding

    grid configure $lbl -sticky w
    grid configure $ent -sticky ew
    grid configure $frm -sticky nsew
    grid columnconfigure . 0 -weight 1
    grid columnconfigure . 1 -weight 1

    $ent insert end $opts(-initialdir)

    # Set bindings
    # <Return> is the same as OK
    bind $w <Return> $okCommand

    # <Escape> is the same as cancel
    bind $w <Escape> $cancelCommand

    # Closing the window is the same as cancel
    wm protocol $w WM_DELETE_WINDOW $cancelCommand
    
    # Fill listbox and bind for browsing
    Refresh $lst $opts(-initialdir)
    
    bind $lst <Return> [namespace code [list Update $ent $lst]]
    bind $lst <Double-ButtonRelease-1> [namespace code [list Update $ent $lst]]

    ::tk::PlaceWindow $w widget [winfo parent $w]

    # Set the min size when the size is known
#    tkwait visibility $w
#    tkChooseDirectory::MinSize $w

    focus $ent
    $ent selection range 0 end
    grab set $w

    # Wait for OK, Cancel or close
    tkwait window $w

    grab release $w
    
    set dir $value($w)
    unset value($w)
    return $dir
}
# tkChooseDirectory::tk_chooseDirectory

proc ::tk::dialog::chooseDir::MinSize { w } {
    set geometry [wm geometry $w]

    regexp {([0-9]*)x([0-9]*)\+} geometry whole width height

    wm minsize $w $width $height
}

proc ::tk::dialog::chooseDir::Done { w why mustexist } {
    variable value

    switch -- $why {
	ok {
	    # If mustexist, validate value
	    set value($w) [$w.e get]
	    if { $mustexist } {
		if { ![file exists $value($w)] } {
		    return
		}
		if { ![file isdirectory $value($w)] } {
		    return
		}
	    }
	}
	cancel {
	    set value($w) ""
	}
    }

    destroy $w
}

proc ::tk::dialog::chooseDir::Refresh { listbox dir } {
    $listbox delete 0 end

    # Find the parent directory; if it is different (ie, we're not
    # already at the root), add a ".." entry
    set parentDir [file dirname $dir]
    if { ![string equal $parentDir $dir] } {
	$listbox insert end ".."
    }
    
    # add the subdirs to the listbox
    foreach f [lsort [glob -nocomplain $dir/*]] {
	if {[file isdirectory $f]} {
	    $listbox insert end "[file tail $f]/"
	}
    }
}

proc ::tk::dialog::chooseDir::Update { entry listbox } {
    set sel [$listbox curselection]
    if { [string equal $sel ""] } {
	return
    }
    set subdir [$listbox get $sel]
    if {[string equal $subdir ".."]} {
	set fullpath [file dirname [$entry get]]
	if { [string equal $fullpath [$entry get]] } {
	    return
	}
    } else {
	set fullpath [file join [$entry get] $subdir]
    }
    $entry delete 0 end
    $entry insert end $fullpath
    Refresh $listbox $fullpath
}
