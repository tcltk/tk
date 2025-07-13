# accessibility.tcl --

# This file defines the 'tk accessible' command for screen reader support
# on X11, Windows, and macOS. It implements an abstraction layer that
# presents a consistent API across the three platforms.

# Copyright © 2024-2025 Kevin Walzer/WordTech Communications LLC.
#
# See the file "license.terms" for information on usage and redistribution
# of this file, and for a DISCLAIMER OF ALL WARRANTIES.
#

namespace eval ::tk::accessible {

    # Check message text on dialog. 
    proc _getdialogtext {w} {
	if {[winfo exists $w.msg]} {
	    return [$w.msg cget -text]
	}
    }

    # Get text in text widget.
    proc _gettext {w} {
	if {[$w tag ranges sel] eq ""} {
	    set data [$w get 1.0 end]	
	}  else {  
	    set data [$w get sel.first sel.last]
	}
	return $data
    }

    # Attempt to verify if treeview is tree or table. This works
    # for simple cases but may not be perfect. 
    proc _checktree {w} {
	if {[expr {"tree" in [$w cget -show]}] eq 1} {
	    return "Tree"
	} else {
	    return "Table"
	}
    }

    # Get data from ttk::treeview.
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

    # Get treeview column names.
    proc _getcolumnnames {w} {
	set columns [$w cget -columns] 
	foreach col $columns {
	    set text [$w heading $col -text]
	    lappend headerlist $text
	}
	return $headerlist
    }	

    # Update data selection for various widgets. 
    proc _updateselection {w} {
	if {[winfo class $w] eq "Listbox"} {
	    set data [$w get [$w curselection]]
	    ::tk::accessible::acc_value $w $data
	    ::tk::accessible::emit_selection_change $w
	}
	if {[winfo class $w] eq "Menu"} {
	    set data [$w entrycget active -label]
	    ::tk::accessible::acc_value $w $data
	    ::tk::accessible::emit_selection_change $w
	}
	if {[winfo class $w] eq "Treeview"} {
	    set data [::tk::accessible::_gettreeviewdata $w]
	    ::tk::accessible::acc_value $w $data
	    ::tk::accessible::emit_selection_change $w
	}
	if {[winfo class $w] eq "Entry" || [winfo class $w] eq "TEntry"}  {
	    set data [$w get]
	    ::tk::accessible::acc_value $w $data
	    ::tk::accessible::emit_selection_change $w
	}
	if {[winfo class $w] eq "TCombobox"} {
	    set data [$w get]
	    ::tk::accessible::acc_value $w $data
	    ::tk::accessible::emit_selection_change $w
	}
	if {[winfo class $w] eq "TNotebook"}  {
	    set data  [$w tab current -text] 
	    ::tk::accessible::acc_value $w $data
	    ::tk::accessible::emit_selection_change $w
	}	
	if {[winfo class $w] eq "Text"}  {
	    set data [::tk::accessible::_gettext $w]
	    ::tk::accessible::acc_value $w $data
	    ::tk::accessible::emit_selection_change $w
	}
	if {[winfo class $w] eq "TProgressbar"}  {
	    set data [::tk::accessible::_getpbvalue $w]
	    ::tk::accessible::acc_value $w $data
	    ::tk::accessible::emit_selection_change $w
	}
	
    }

    # Increment values in various widgets in response to keypress events. 
    proc _updatescale {w key} {
	if {[winfo class $w] eq "Scale"} {
	    switch -- $key {
		Right {
		    tk::ScaleIncrement $w down little noRepeat
		    set data [$w get]
		    ::tk::accessible::acc_value $w $data
		    ::tk::accessible::emit_selection_change $w
		}
		Left {
		    tk::ScaleIncrement $w up little noRepeat
		    set data [$w get]
		    ::tk::accessible::acc_value $w $data
		    ::tk::accessible::emit_selection_change $w
		}
	    }
	}
	if {[winfo class $w] eq "TScale"} {
	    switch -- $key {
		Right {
		    ttk::scale::Increment $w 1
		    set data [$w get]
		    ::tk::accessible::acc_value $w $data
		    ::tk::accessible::emit_selection_change $w
		}
		Left {
		    ttk::scale::Increment $w -1
		    set data [$w get]
		    ::tk::accessible::acc_value $w $data
		    ::tk::accessible::emit_selection_change $w
		}
	    }
	}

	if {[winfo class $w] eq "Spinbox"} {
	    switch -- $key {
		Up {
		    $w invoke buttonup
		    set data [$w get]
		    ::tk::accessible::acc_value $w $data
		    ::tk::accessible::emit_selection_change $w
		}
		Down {
		    $w invoke buttondown
		    set data [$w get]
		    ::tk::accessible::acc_value $w $data
		    ::tk::accessible::emit_selection_change $w
		}
	    }
	}
	if {[winfo class $w] eq "TSpinbox"} {
	    switch -- $key {
		Up {
		    ttk::spinbox::Spin $w +1 
		    set data [$w get]
		    ::tk::accessible::acc_value $w $data
		    ::tk::accessible::emit_selection_change $w
		}
		Down {
		    ttk::spinbox::Spin $w -1 
		    set data [$w get]
		    ::tk::accessible::acc_value $w $data
		    ::tk::accessible::emit_selection_change $w
		}
	    }
	}
	if {[winfo class $w] eq "TCombobox"} {
	    switch -- $key {
		Down {
		    ttk::combobox::Post $w
		    set data [$w get]
		}
		Escape {
		    ttk::combobox::Unpost $w
		    $w selection range 0 end
		    if {[tk windowingsystem] eq "aqua"} {
			event generate <Command-a>
		    } else {
			event generate <Control-a>
		    }
		    set data [$w get]
		    ::tk::accessible::acc_value $w $data
		    ::tk::accessible::emit_selection_change $w
		}
	    }
	}
	if {[winfo class $w] eq "TNotebook"} {
	    switch -- $key {
		Right {
		    { ttk::notebook::CycleTab %W  1; break }
		    set data [$w get]
		    ::tk::accessible::acc_value $w $data
		    ::tk::accessible::emit_selection_change $w
		}
		Left {
		    ttk::scale::Increment $w -1
		    set data [$w get]
		    ::tk::accessible::acc_value $w $data
		    ::tk::accessible::emit_selection_change $w
		}
	    }
	}

	
    }

    # Get value of progress bar.
    proc _getpbvalue {w} {
	
	# This variable exists if the progress bar is running, otherwise no.
	variable ::ttk::progressbar::Timers
	
	if {![info exists ::ttk::progressbar::Timers($w)]} {
	    return "not running"
	}
	if {[info exists ::ttk::progressbar::Timers($w)]} {
	    return "busy"
	}
    }
    
    # Some widgets will not respond to keypress events unless
    # they have focus. Force Tk focus on the widget that currently has
    # accessibility focus if needed.
    
    proc _forceTkFocus {w} {
	
	if {[tk windowingsystem] eq "aqua"} {
	    if {[winfo class $w] eq "Scale" || [winfo class $w] eq "TScale" || [winfo class $w] eq "Spinbox" || [winfo class $w] eq "TSpinbox" || [winfo class $w] eq "Listbox" || [winfo class $w] eq "Treeview" || [winfo class $w] eq "TProgressbar"} {
		if {[focus] ne $w} {
		    focus -force $w
		}
	    } else {
		return
	    }
	}
	if {[tk windowingsystem] eq "win32"} {
	    ::tk::accessible::emit_focus_change $w
	}
    }
    
    # Set initial accessible attributes and add binding to <Map> event.
    # If the accessibility role is already set, return because
    # we only want these to fire once.
    
    proc _init {w role name description value state action} {
	if {[catch {::tk::accessible::get_acc_role $w} msg]} {
	    if {$msg == $role} {
		return
	    }
	}
	if {[tk windowingsystem] eq "win32"} {
	    #This is necessary to ensure correct accessible keyboard navigation
	    $w configure -takefocus 1
	}
	
	::tk::accessible::acc_role $w $role
	::tk::accessible::acc_name $w $name
	::tk::accessible::acc_description $w $description
	::tk::accessible::acc_value $w $value  
	::tk::accessible::acc_state $w $state
	::tk::accessible::acc_action $w $action
	
    }

    # Button/TButton bindings.
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
    
    # Menubutton/TMButton bindings.
    bind Menubutton <Map> {+::tk::accessible::_init \
			       %W \
			       Button \
			       Button \
			       [%W cget -text] \
			       {} \
			       [%W cget -state] \
			       {%W invoke}\
			   }
    bind TMenubutton <Map> {+::tk::accessible::_init \
				%W \
				Button \
				Button \
				[%W cget -text] \
				{} \
				[%W cget -state] \
				{%W invoke}\
			    }
    
    # Canvas bindings. 
    bind Canvas <Map> {+::tk::accessible::_init \
			   %W \
			   Canvas \
			   Canvas \
			   Canvas \
			   {} \
			   {} \
			   {}\
		       }
    
    # Checkbutton/TCheckbutton bindings.
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
    
    # Combobox bindings.			    
    bind TCombobox <Map> {+::tk::accessible::_init \
			      %W \
			      Combobox \
			      Combobox \
			      Combobox \
			      [%W get] \
			      [%W cget -state] \
			      {} \
			  }
    
    # Dialog bindings.
    bind Dialog <Map> {+::tk::accessible::_init\
			   %W \
			   Dialog \
			   [wm title %W] \
			   [::tk::accessible::_getdialogtext %W] \
			   {} \
			   {} \
			   {}\
		       }

    # Entry/TEntry bindings.
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

    # Listbox bindings.
    bind Listbox <Map> {+::tk::accessible::_init \
			    %W \
			    Listbox \
			    Listbox \
			    Listbox \
			    [%W get [%W curselection]] \
			    [%W cget -state]\
			    {%W invoke}\
			}

    # Progressbar bindings. 
    bind TProgressbar <Map> {+::tk::accessible::_init \
				 %W \
				 Progressbar \
				 Progressbar \
				 Progressbar \
				 [::tk::accessible::_getpbvalue %W] \
				 [%W state] \
				 {}\
			     }

    # Radiobutton/TRadiobutton bindings.
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

    # Scale/TScale bindings.
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

    # Menu bindings - macOS/Windows menus are native and
    # already accessible-enabled.
    if {[tk windowingsystem] eq "x11"} {
	bind Menu <Map> {+::tk::accessible::_init \
			     %W \
			     Menu \
			     [%W entrycget active -label] \
			     [%W entrycget active -label] \
			     {} \
			     {} \
			     {%W invoke}\
			 }
    }
    
    # Scrollbar/TScrollbar bindings.
    bind Scrollbar <Map> {+::tk::accessible::_init \
			      %W \
			      Scrollbar \
			      Scrollbar \
			      Scrollbar \
			      {} \
			      {} \
			      {}\
			  }
    bind TScrollbar <Map> {+::tk::accessible::_init \
			       %W \
			       Scrollbar \
			       Scrollbar \
			       Scrollbar \
			       {} \
			       {} \
			       {}\
			   }
    
    # Spinbox/TSpinbox bindings.
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


    # Treeview bindings.
    bind Treeview <Map> {+::tk::accessible::_init \
			     %W \
			     [::tk::accessible::_checktree %W] \
			     [::tk::accessible::_checktree %W] \
			     [::tk::accessible::_getcolumnnames %W] \
			     [::tk::accessible::_gettreeviewdata %W] \
			     [%W state] \
			     {ttk::treeview::Press %W %x %y }\
			 }

    # Text bindings.
    bind Text <Map> {+::tk::accessible::_init \
			 %W \
			 Text \
			 Text \
			 Text \
			 [::tk::accessible::_gettext %W] \
			 [%W cget -state] \
			 {}\
		     }
    
    # Label/TLabel bindings.
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

    # Notebook bindings - bind to the <<NotebookTabChanged>> event rather
    # than <Map> because this event is generated before the <Map> event, 
    # which returns an error because the accessibility data has not been
    # initialized yet. 
    bind TNotebook <<NotebookTabChanged>> {+::tk::accessible::_init \
					       %W \
					       Notebook \
					       Notebook \
					       Notebook \
					       [%W tab current -text] \
					       {} \
					       {}\
					   }

    # Activate accessibility object when mapped.
    if {[tk windowingsystem] eq "win32"} {
	bind Toplevel <Map> {
	    after 0 [list ::tk::accessible::add_acc_object %W]
	}
    }
    bind all <Map> {+::tk::accessible::add_acc_object %W}
    
    # Various bindings to capture data/selection changes for
    # widgets that support returning a value. 

    #Selection changes.
    bind Listbox <<ListboxSelect>> {+::tk::accessible::_updateselection %W} 
    bind Treeview <<TreeviewSelect>> {+::tk::accessible::_updateselection %W}
    bind TCombobox <<ComboboxSelected>> {+::tk::accessible::_updateselection %W}
    bind Text <<Selection>> {+::tk::accessible::_updateselection %W}

    
    #Capture value changes from scale widgets.
    bind Scale <Right> {+::tk::accessible::_updatescale %W Right}
    bind Scale <Left> {+::tk::accessible::_updatescale %W Left}
    bind TScale <Right> {+::tk::accessible::_updatescale %W Right}
    bind TScale <Left> {+::tk::accessible::_updatescale %W Left}

    # On macOS, the ttk::spinbox returns the wrong accessibility role
    # because of how it is constructed. If VoiceOver is running,
    # alias the ttk::spinbox to the core Tk spinbox.
    if {[tk windowingsystem] eq "aqua"} {
	set result [::tk::accessible::check_screenreader]
	if {$result > 0} {
	    interp alias {} ::ttk::spinbox {} ::tk::spinbox
	}
    }
    
    # Capture value changes from spinbox widgets.
    bind Spinbox <Up> {+::tk::accessible::_updatescale %W Up}
    bind Spinbox <Down> {+::tk::accessible::_updatescale %W Down}
    bind TSpinbox <Up> {+::tk::accessible::_updatescale %W Up}
    bind TSpinbox <Down> {+::tk::accessible::_updatescale %W Down}

    #Capture notebook selection
    bind TNotebook <Map> {+::ttk::notebook::enableTraversal %W}
    bind TNotebook <<NotebookTabChanged>> {+::tk::accessible::_updateselection %W}

    #
    # VoiceOver/macOS does not respond to the virtual <<SelectAll>> event
    # in entry widgets but does respond to the corresponding keysyms.
    #
    
    if {[tk windowingsystem] eq "aqua"} {
	bind Entry <Command-a> {+::tk::accessible::_updateselection %W}
	bind TEntry <Command-a> {+::tk::accessible::_updateselection %W}
    } else  {
	bind Entry <Control-a> {+::tk::accessible::_updateselection %W}
	bind TEntry <Control-a> {+::tk::accessible::_updateselection %W}
    }

    #Progressbar updates.
    bind TProgressbar <FocusIn> {+::tk::accessible::_updateselection %W}


    # Help text for widgets that require additional direction
    # on keyboard navigation - these widgets will use standard keyboard
    # navigation when they obtain focus rather than the accessibility
    # keyboard shortcuts. We are mostly limiting the accessibility tree to one
    # level - toplevel window and child windows - to reduce the complexity of
    # the implementation, which is tied tighly to Tk windows. Component
    # elements of many widgets such listbox or treeview rows are not exposed as
    # Tk windows, and there is no simple way to expose them to the platforms'
    # accessibility API's directly, but they can be navigated via the keyboard
    # and their data (obtained via selection events) can be piped to the
    # screen reader for vocalization. The help text here assists the user
    # in switching to the standard keys for navigation as needed. 

    bind Listbox <Map> {+::tk::accessible::acc_help %W "To navigate, click the mouse or trackpad and then use the standard Up-Arrow and Down-Arrow keys."}
    bind Treeview <Map> {+::tk::accessible::acc_help %W "To navigate, click the mouse or trackpad and then use the standard Up-Arrow and Down-Arrow keys. To open or close a tree node, click the Space key."}
    bind Entry <Map> {+::tk::accessible::acc_help %W "To navigate, click the mouse or trackpad and then use standard keyboard navigation. To hear the contents of the entry field, select all."}
    bind TEntry <Map> {+::tk::accessible::acc_help %W "To navigate, click the mouse or trackpad and then use standard keyboard navigation. To hear the contents of the entry field, select all."}
    bind Scale <Map> {+::tk::accessible::acc_help %W "Click the right or left arrows to move the scale."}
    bind TScale <Map> {+::tk::accessible::acc_help %W "Click the right or left arrows to move the scale."}
    bind Spinbox <Map> {+::tk::accessible::acc_help %W "Click the up or down arrows to change the value."}
    bind TSpinbox <Map> {+::tk::accessible::acc_help %W "Click the up or down arrows to change the value."}
    bind Canvas <Map> {+::tk::accessible::acc_help %W "The canvas widget is not accessible."}
    bind Scrollbar <Map> {+::tk::accessible::acc_help %W "Use the touchpad or mouse wheel to move the scrollbar."}
    bind TScrollbar <Map> {+::tk::accessible::acc_help %W "Use the touchpad or mouse wheel to move the scrollbar."}
    bind Menubutton <Map> {+::tk::accessible::acc_help %W "Use the touchpad or mouse wheel to pop up the menu."}
    bind TMenubutton <Map> {+::tk::accessible::acc_help %W "Use the touchpad or mouse wheel to pop up the menu."}
    bind TNotebook <Map> {+::tk::accessible::acc_help %W "Use the Tab and Right/Left arrow keys to navigate between notebook tabs."}
    bind Text <Map> {+::tk::accessible::acc_help %W "Use normal keyboard shortcuts to navigate the text widget."}
    
    if {[tk windowingsystem] eq "win32"} {
	bind all <FocusIn> {+::tk::accessible::_forceTkFocus %W}
    }
    
    # Finally, export the main commands.
    namespace export acc_role acc_name acc_description acc_value acc_state acc_action acc_help get_acc_role get_acc_name get_acc_description get_acc_value get_acc_state get_acc_action get_acc_help add_acc_object emit_selection_change check_screenreader emit_focus_change
    namespace ensemble create
}

# Add these commands to the tk command ensemble: tk accessible.
namespace ensemble configure tk -map \
    [dict merge [namespace ensemble configure tk -map] \
	 {accessible ::tk::accessible}]



