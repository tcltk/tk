# choosedir.tcl --
#
#	Choose directory dialog implementation for Unix/Mac.  Adapted from
#	Christopher Nelson's (chris@pinebush.com) implementation.
#
# Copyright (c) 1998-2000 by Scriptics Corporation.
# All rights reserved.
# 
# RCS: @(#) $Id: choosedir.tcl,v 1.1 2000/01/27 00:23:10 ericm Exp $

package require opt

namespace eval ::tkChooseDirectory {
    variable value
}

::tcl::OptProc ::tkChooseDirectory::tk_chooseDirectory {
    {-initialdir -string ""  
            "Initial directory for browser"}
    {-mustexist              
            "If specified, user can't type in a new directory"}
    {-parent     -string "."
            "Parent window for browser"}
    {-title      -string "Choose Directory"
            "Title for browser window"}
} {
    # Handle default directory
    if {[string length $initialdir] == 0} {
	set initialdir [pwd]
    }

    # Handle default parent window
    if {[string compare $parent "."] == 0} {
	set parent ""
    }

    set w [toplevel $parent.choosedirectory]
    wm title $w $title

    # Commands for various bindings (which follow)
    set okCommand  [namespace code \
	    [list Done $w ok ::tkChooseDirectory::value($w)]]

    set cancelCommand  [namespace code \
	    [list Done $w cancel ::tkChooseDirectory::value($w)]]

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

    $ent insert end $initialdir

    # Set bindings
    # <Return> is the same as OK
    bind $w <Return> $okCommand

    # <Escape> is the same as cancel
    bind $w <Escape> $cancelCommand

    # Closing the window is the same as cancel
    wm protocol $w WM_DELETE_WINDOW $cancelCommand
    
    # Fill listbox and bind for browsing
    Refresh $lst $initialdir
    
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
    
    set dir $::tkChooseDirectory::value($w)
    unset ::tkChooseDirectory::value($w)
    return $dir
}
# tkChooseDirectory::tk_chooseDirectory

proc ::tkChooseDirectory::MinSize { w } {
    set geometry [wm geometry $w]

    regexp {([0-9]*)x([0-9]*)\+} geometry whole width height

    wm minsize $w $width $height
}

proc ::tkChooseDirectory::Done { w why varName } {
    variable value

    switch -- $why {
	ok {
	    # If mustexist, validate with [cd]
	    set value($w) [$w.e get]
	}
	cancel {
	    set value($w) ""
	}
    }

    destroy $w
}

proc ::tkChooseDirectory::Refresh { listbox dir } {
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

proc ::tkChooseDirectory::Update { entry listbox } {
    set sel [$listbox curselection]
    if { [string equal $sel ""] } {
	return
    }
    set subdir [$listbox get $sel]
    if {[string compare $subdir ".."] == 0} {
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

# Some test code
if {[string compare [info script] $argv0] == 0} {
    catch {rename ::tk_chooseDirectory tk_chooseDir}
    
    proc tk_chooseDirectory { args } {
	uplevel ::tkChooseDirectory::tk_chooseDirectory $args
    }

    wm withdraw .
    set dir [tk_chooseDirectory -initialdir [pwd] \
	    -title "Choose a directory"]
    tk_messageBox -message "dir:<<$dir>>"
    exit
}
