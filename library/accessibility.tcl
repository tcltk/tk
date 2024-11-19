# accessibility.tcl --

# This file defines the 'tk accessible' command for screen reader support
# on X11, Windows, and macOS. It implements an abstraction layer that
# presents a consistent API across the three platforms.

# Copyright © 2024 Kevin Walzer/WordTech Communications LLC.
#
# See the file "license.terms" for information on usage and redistribution
# of this file, and for a DISCLAIMER OF ALL WARRANTIES.
#


namespace eval ::tk::accessible {

    #check message text on dialog 
    proc _getdialogtext {w} {
	if {[winfo exists $w.msg]} {
	    return [$w.msg cget -text]
	}
    }

    #check to see if attributes have been applied before binding default values 
    proc _checkattributes {w} {
	if {[catch {info exists [::tk::accessible::get_acc_role $w]} msg]} {
	    return
	}
    }

    bind all <Map> {
	
	variable acc_class
	set acc_class [winfo class %W]
	
	switch -- $acc_class {
	    Button -
	    TButton {
		::tk::accessible::_checkattributes %W
		::tk::accessible::acc_role %W Button
		::tk::accessible::acc_name %W Button
		::tk::accessible::acc_description %W  [%W cget -text]
		::tk::accessible::acc_value %W {}
		::tk::accessible::acc_state %W  [%W cget -state]
		::tk::accessible::acc_action %W  {%W invoke}
	    }
	    Canvas {}
	    Dialog {
		::tk::accessible::_checkattributes %W
		::tk::accessible::acc_role %W Dialog
		::tk::accessible::acc_name %W [wm title %W]
		::tk::accessible::acc_description %W [::tk::accessible::_getdialogtext %W]
		::tk::accessible::acc_value %W  {}
		::tk::accessible::acc_state %W  {}
		::tk::accessible::acc_action %W  {}
	    }
	    Entry -
	    TEntry {
		::tk::accessible::_checkattributes %W
		::tk::accessible::acc_role %W Entry
		::tk::accessible::acc_name %W Entry
		::tk::accessible::acc_description %W Entry
		::tk::accessible::acc_value %W  [%W get 0 end]
		::tk::accessible::acc_state %W  [%W cget -state]
		::tk::accessible::acc_action %W  {}
	    }
	    Listbox {
		::tk::accessible::_checkattributes %W
		::tk::accessible::acc_role %W Table
		::tk::accessible::acc_name %W Table
		::tk::accessible::acc_description %W Table
		::tk::accessible::acc_value %W [%W get [%W curselection]]
		::tk::accessible::acc_state %W  [%W cget -state]
		::tk::accessible::acc_action %W  {}
	    }
	    Menu {
	        #menus on macOS are native and accessibility-ready
		if {[tk windowingsystem] ne "aqua"} {
		    ::tk::accessible::_checkattributes %W
		    ::tk::accessible::acc_role %W Menu
		    ::tk::accessible::acc_name %W [%W entrycget active -label]
		    ::tk::accessible::acc_description %W [%W entrycget active\
							      -label]
		    ::tk::accessible::acc_value %W {}
		    ::tk::accessible::acc_state %W  [%W cget -state]
		    ::tk::accessible::acc_action %W  {%W invoke}
		}	
	    }
	    
	    Scale -
	    TScale {
		::tk::accessible::_checkattributes %W
		::tk::accessible::acc_role %W Scale
		::tk::accessible::acc_name %W Scale
		::tk::accessible::acc_description %W Scale
		::tk::accessible::acc_value %W [%W get]
		::tk::accessible::acc_state %W  [%W cget -state]
		::tk::accessible::acc_action %W  {%W invoke}
	    }
	}
    }

    namespace export acc_role acc_name acc_description acc_value acc_state acc_action get_acc_role get_acc_name get_acc_description get_acc_value get_acc_state get_acc_action
    namespace ensemble create
}

#Add these commands to the tk command ensemble: tk accessible
namespace ensemble configure tk -map \
    [dict merge [namespace ensemble configure tk -map] \
	 {accessible ::tk::accessible}]
