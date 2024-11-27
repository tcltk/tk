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

    #get text in text widget
    proc _gettext {w} {
	if {[$w tag ranges sel] eq ""} {
		set data [$w get 1.0 end]	
	    }  else {  
		set data [$w get sel.first sel.last]
	    }
	return $data
    }


    #Set initial accessible attributes and add binding to <Map> event. If the accessibility role is already set,
    #return because we only want these to fire once.
    
    proc _init {w role name description value state action} {
	if {[catch {::tk::accessible::get_acc_role $w} msg]} {
	    if {$msg == $role} {
		return
	    }
	}
	::tk::accessible::acc_role $w $role
	::tk::accessible::acc_name $w $name
	::tk::accessible::acc_description $w $description
	::tk::accessible::acc_value $w $value  
	::tk::accessible::acc_state $w $state
	::tk::accessible::acc_action $w $action
    }

    #Button bindings
    bind Button <Map> {+::tk::accessible::_init \
			   %W \
			   Button \
			   Button \
			   [%W cget -text] \
			   {} \
			   [%W cget -state] \
			   {%W invoke}\
		       }
    #Canvas bindings
    bind Canvas <Map> {+::tk::accessible::_init \
			   %W \
			   Canvas \
			   Canvas \
			   Canvas \
			   {} \
			   {} \
			   {}\
		       }

    #Dialog bindings
    bind Dialog <Map> {+::tk::accessible::_init\
			   %W \
			   Dialog \
			   [wm title %W] \
			   [::tk::accessible::_getdialogtext %W] \
			   {} \
			   {} \
			   {}\
		       }

    #Entry/TEntry bindings
    bind Entry <Map> {+::tk::accessible::_init \
			  %W \
			  Entry \
			  Entry \
			  Entry \
			  [%W get 0 end] \
			  [%W cget -state] \
			  {} \
		      }
    bind TEntry <Map> {+::tk::accessible::_init \
			   %W \
			   Entry \
			   Entry \
			   Entry \
			   [%W get] \
			   [%W cget -state]\
			   {} \
		       }

    #Listbox bindings
    bind Listbox <Map> {+::tk::accessible::_init \
			    %W \
			    Table \
			    Table \
			    Table\
			    [%W get [%W curselection]] \
			    [%W cget -state]\
			    {}\
			}

    #Radiobutton bindings
    bind Radiobutton <Map> {+::tk::accessible::_init \
				%W \
				Radiobutton \
				Radiobutton \
				Radiobutton \
				[set [%W cget -variable]] \
				[%W cget -state] \
				{}\
			    }

    #Scale/TScale bindings
    bind Scale <Map> {+::tk::accessible::_init \
			  %W \
			  Scale \
			  Scale \
			  Scale \
			  [%W get] [%W cget -state]\
			  {%W set}\
		      }
    bind TScale <Map> {+::tk::accessible::_init \
			   %W \
			   Scale \
			   Scale \
			   Scale \
			   [%W get] \
			   [%W state] \
			   {%W set} \
		       }

    #Menu bindings - macOS menus are native and already accessible-enabled
    if {[tk windowingsystem] ne "aqua"} {
	bind Menu <Map> {+::tk::accessible::_init \
			     %W \
			     Menu \
			     [%W entrycget active -label] \
			     [%W entrycget active -label] \
			     {} \
			     [%W cget -state] \
			     {%W invoke}\
			 }
    }

    #Scrollbar/TScrollbar bindings
    bind Scrollbar <Map> {+::tk::accessible::_init \
			      %W \
			      Scrollbar \
			      Scrollbar \
			      Scrollbar \
			      [%W get] \
			      [%W cget -state] \
			      {%W cget -command}\
			  }
    bind TScrollbar <Map> {+::tk::accessible::_init \
			       %W \
			       Scrollbar \
			       Scrollbar \
			       Scrollbar \
			       [%W get] \
			       [%W cget -state] \
			       {%W cget -command}\
			   }
    #Spinbox/TSpinbox bindings
    bind  Spinbox <Map> {+::tk::accessible::_init \
			     %W \
			     Spinbox \
			     Spinbox \
			     Spinbox \
			     [%W get] \
			     [%W cget -state] \
			     {%W cget -command}\
			 }
    bind  TSpinbox <Map> {+::tk::accessible::_init \
			      %W \
			      Spinbox \
			      Spinbox \
			      Spinbox \
			      [%W get] \
			      [%W state] \
			      {%W cget -command}\
			  }

    #Text bindings
    bind Text <Map> {+::tk::accessible::_init \
			 %W \
			 Text \
			 Text \
			 Text \
			 [::tk::accessible::_gettext %W] \
			 [%W cget -state] \
			 {}\
		     }


    #Export the main commands.
    namespace export acc_role acc_name acc_description acc_value acc_state acc_action get_acc_role get_acc_name get_acc_description get_acc_value get_acc_state get_acc_action
    namespace ensemble create
}

#Add these commands to the tk command ensemble: tk accessible
namespace ensemble configure tk -map \
    [dict merge [namespace ensemble configure tk -map] \
	 {accessible ::tk::accessible}]
