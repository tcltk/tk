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

    #check if tree or table
    proc _checktree {w} {
	if {[expr {"tree" in [$w cget -show]}] eq 1} {
	    return Tree
	} else {
	    return Table
	}
    }

    #Get data from ttk::treeview for use in the API
    proc _gettreeviewdata {w} {
	if {[::tk::accessible::_checktree $w] eq "Tree"} {
	    #Tree
	    set data [$w item [$w selection] -text]
	} else {
	    #Table
	    set data [$w item [$w selection] -values]
	}
	return $data
    }

    #update data selection
    proc _updateselection {w} {
	if {[winfo class $w] eq "Listbox"} {
	    set data [$w get [$w curselection]]
	    ::tk::accessible::acc_value $w $data
	    ::tk::accessible::emit_selection_change $w
	}
	if {[winfo class $w] eq "Treeview"} {
	    set data [::tk::accessible::_gettreeviewdata $w]
	    ::tk::accessible::acc_value $w $data
	    ::tk::accessible::emit_selection_change $w
	}
    }

    #force Tk focus on the widget that currently has accessibility focus
    proc _forceTkFocus {w} {
	if {[focus] ne $w} {
	    focus $w
	}
    }

	
    #Set initial accessible attributes and add binding to <Map> event.
    #If the accessibility role is already set, return because
    #we only want these to fire once.
    
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

    #Button/TButton bindings
    bind Button <Map> {+::tk::accessible::_init \
			   %W \
			   Button \
			   Button \
			   [%W cget -text] \
			   {} \
			   [%W cget -state] \
			   {%W invoke}\
		       }
    bind TButton <Map> {+::tk::accessible::_init \
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
    
    #Checkbutton/TCheckbutton bindings
    bind Checkbutton <Map> {+::tk::accessible::_init \
				%W \
				Checkbutton \
				Checkbutton \
				[%W cget -text] \
				[set [%W cget -variable]] \
				[%W cget -state] \
				{%W invoke}\
			    }
    bind TCheckbutton <Map> {+::tk::accessible::_init \
				 %W \
				 Checkbutton \
				 Checkbutton \
				 [%W cget -text] \
				 [set [%W cget -variable]] \
				 [%W cget -state] \
				 {%W invoke}\
			     }
    #combobox bindings			    
    bind TCombobox <Map> {+::tk::accessible::_init \
			      %W \
			      Combobox \
			      Combobox \
			      Combobox \
			      [%W get] \
			      [%W cget -state] \
			      {} \
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
			  [%W get] \
			  [%W cget -state] \
			  {} \
		      }
    bind TEntry <Map> {+::tk::accessible::_init \
			   %W \
			   Entry \
			   Entry \
			   Entry \
			   [%W get] \
			   [%W state]\
			   {} \
		       }

    #Listbox bindings
    bind Listbox <Map> {+::tk::accessible::_init \
			    %W \
			    Listbox \
			    Listbox \
			    Listbox \
			    [%W get [%W curselection]] \
			    [%W cget -state]\
			    {%W invoke}\
			}

    #Progressbar
    bind TProgressbar <Map> {+::tk::accessible::_init \
				 %W \
				 Progressbar \
				 Progressbar \
				 Progressbar \
				 [%W cget -value] \
				 [%W state] \
				 {}\
			     }

    #Radiobutton/TRadiobutton bindings
    bind Radiobutton <Map> {+::tk::accessible::_init \
				%W \
				Radiobutton \
				Radiobutton \
				[%W cget -text] \
				[%W cget -variable] \
				[%W cget -state] \
				{%W invoke}\
			    }
    bind TRadiobutton <Map> {+::tk::accessible::_init \
				 %W \
				 Radiobutton \
				 Radiobutton \
				 [%W cget -text] \
				 [%W cget -variable] \
				 [%W cget -state] \
				 {% invoke}\
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
			   [%W cget -state] \
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

    #Notebook bindings
    #make keyboard navigation the default behavior
    bind TNotebook <Map> {+::ttk::notebook::enableTraversal %W}
    
    bind TNotebook <Map> {+::tk::accessible::_init \
			      %W \
			      Notebook \
			      Notebook \
			      Notebook \
			      [%W tab current -text] \
			      {} \
			      {}\
			  }
    
    #Scrollbar/TScrollbar bindings
    bind Scrollbar <Map> {+::tk::accessible::_init \
			      %W \
			      Scrollbar \
			      Scrollbar \
			      Scrollbar \
			      [%W get] \
			      {} \
			      {}\
			  }
    bind TScrollbar <Map> {+::tk::accessible::_init \
			       %W \
			       Scrollbar \
			       Scrollbar \
			       Scrollbar \
			       [%W get] \
			       [%W state] \
			       {}\
			   }
    #Spinbox/TSpinbox bindings
    bind Spinbox <Map> {+::tk::accessible::_init \
			    %W \
			    Spinbox \
			    Spinbox \
			    Spinbox \
			    [%W get] \
			    [%W cget -state] \
			    {%W cget -command}\
			}
    bind TSpinbox <Map> {+::tk::accessible::_init \
			     %W \
			     Spinbox \
			     Spinbox \
			     Spinbox \
			     [%W get] \
			     [%W state] \
			     {%W cget -command}\
			 }


    #Treeview bindings
    bind Treeview <Map> {+::tk::accessible::_init \
			     %W \
			     [::tk::accessible::_checktree %W] \
			     [::tk::accessible::_checktree %W] \
			     [::tk::accessible::_checktree %W] \
			     [::tk::accessible::_gettreeviewdata %W] \
			     [%W state] \
			     {ttk::treeview::Press %W %x %y }\
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
    
    #Label/TLabel bindings
    bind Label <Map>    {+::tk::accessible::_init \
			     %W \
			     Label \
			     Label \
			     Label \
			     [%W cget -text] \
			     {}\
			     {}\
			 }

    bind TLabel <Map>    {+::tk::accessible::_init \
			      %W \
			      Label \
			      Label \
			      Label \
			      [%W cget -text] \
			      {}\
			      {}\
			  }
    
    
    bind all <Map> {+::tk::accessible::add_acc_object %W}
    bind Listbox <<ListboxSelect>> {+::tk::accessible::_updateselection %W}
    bind Listbox <Map> {+::tk::accessible::acc_help %W "To navigate the listbox, use the standard Up-Arrow and Down-Arrow keys."}
    bind Treeview <<TreeviewSelect>> {+::tk::accessible::_updateselection %W}
    bind Treeview <Map> {+::tk::accessible::acc_help %W "To navigate, click the mouse or trackpad and then use the standard Up-Arrow and Down-Arrow keys. To open or close a tree node, click the Space key."}

    #Export the main commands.
    namespace export acc_role acc_name acc_description acc_value acc_state acc_action acc_help get_acc_role get_acc_name get_acc_description get_acc_value get_acc_state get_acc_action get_acc_help add_acc_object
    namespace ensemble create
}

#Add these commands to the tk command ensemble: tk accessible
namespace ensemble configure tk -map \
    [dict merge [namespace ensemble configure tk -map] \
	 {accessible ::tk::accessible}]



