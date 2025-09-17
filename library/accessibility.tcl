# accessibility.tcl --

# This file defines the 'tk accessible' command for screen reader support
# on X11, Windows, and macOS. It implements an abstraction layer that
# presents a consistent API across the three platforms.

# Copyright © 2009 Allen B. Taylor.
# Copyright © 2024-2025 Kevin Walzer/WordTech Communications LLC.
#
# See the file "license.terms" for information on usage and redistribution
# of this file, and for a DISCLAIMER OF ALL WARRANTIES.
#

if {[tk windowingsystem] eq "x11" && ([::tk::accessible::check_screenreader] eq 0 || [::tk::accessible::check_screenreader] eq "")} {
    # On X11, do not load if screen reader is not running - macOS and Windows
    # handle this automatically.
    
    return
} 
if {[tk windowingsystem] eq "x11" && [::tk::accessible::check_screenreader] eq 1} { 

    # Add border to all X11 widgets with accessible focus. A highlight rectangle
    # is drawn over focused widgets by the screen reader app on 
    # macOS and Windows (VoiceOver, NVDA), but not on X11. Configuring
    # "-relief groove" and binding to FocusIn/Out events is the cleanest
    # way to accomplish this.

    namespace eval ::tk::accessible {
	variable origConfig
	array set origConfig {}

	# Apply to classic Tk widgets
	proc ClassicFocusIn {w} {
	    variable origConfig
	    if {![info exists origConfig($w)]} {
		set origConfig($w) [list [$w cget -relief] [$w cget -borderwidth]]
	    }
	    $w configure -relief groove -borderwidth 2
	}

	proc ClassicFocusOut {w} {
	    variable origConfig
	    if {[info exists origConfig($w)]} {
		lassign $origConfig($w) relief border
		$w configure -relief $relief -borderwidth $border
	    }
	}

	# Apply focus bindings to a widget
	proc AddClassic {w} {
	    bindtags $w [linsert [bindtags $w] 0 FocusBorder]
	}

	# Setup global ttk styles
	proc InitTtk {} {
	    foreach class {TButton TEntry TCombobox TCheckbutton TRadiobutton \
			       Treeview TScrollbar TScale TSpinbox TLabel} {
		ttk::style map $class \
		    -relief {focus groove !focus flat} \
		    -borderwidth {focus 2 !focus 1}
	    }
	}

	# Install focusborder bindtag for classic widgets
	bind FocusBorder <FocusIn>  {+::tk::accessible::ClassicFocusIn %W}
	bind FocusBorder <FocusOut> {+::tk::accessible::ClassicFocusOut %W}

	# Install ttk mappings
	::tk::accessible::InitTtk

	# Save the original widget commands and replace with wrappers
	foreach wtype {button entry text listbox scale spinbox scrollbar label radiobutton checkbutton} {
	    if {[llength [info commands ::tk::accessible::orig_$wtype]] == 0} {
		rename ::$wtype ::tk::accessible::orig_$wtype
		proc ::$wtype {args} [string map [list %TYPE% $wtype] {
		    # Create the widget with the original command
		    set w [::tk::accessible::orig_%TYPE% {*}$args]
		    # Add focus bindings
		    ::tk::accessible::AddClassic $w
		    return $w
		}]
	    }
	}
    }
}

namespace eval ::tk::accessible {
	
    # Check message text on dialog.
    proc _getdialogtext {w} {
	if {[winfo exists $w.msg]} {
	    return [$w.msg cget -text]
	}
    }
    
    # Get text in text widget.
    proc _gettext {w} {
	if {[winfo class $w] ne "Text"} {
	    return ""
	}
	if {[$w tag ranges sel] eq ""} {
	    set data [$w get 1.0 end]	
	}  else {  
	    set data [$w get sel.first sel.last]
	}
	return $data
    }

    # Get text in entry widget.
    proc _getentrytext {w} {
	set data [$w get]
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
	set values [$w item [lindex [$w selection] 0] -values]
	return $values
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

    # Get selection status from radiobuttons.
    proc _getradiodata {w} {
	if {[winfo class $w] eq "Radiobutton" || [winfo class $w] eq "TRadiobutton"} {
	    set var [$w cget -variable]
	    if {$var eq ""} {
		return "not selected"
	    }
	    set varvalue [set $var]
	    set val [$w cget -value]
	    if {$varvalue eq $val} {
		return "selected"
	    } else {
		return "not selected"
	    }
	}
    }

        # Get selection status from checkbuttons.
       proc _getcheckdata {w} {
	if {[winfo class $w] eq "Checkbutton" || [winfo class $w] eq "TCheckbutton"} {
	    set var [$w cget -variable]
	    if {$var eq ""} {
		return "0"
	    }
	    set varvalue [set $var]
	    set val [$w cget -onvalue]
	    if {$varvalue eq $val} {
		return "1"
	    } else {
		return "0"
	    }
	}
    }

	    
    # Update data selection for various widgets. 
    proc _updateselection {w} {
	if {[winfo class $w] eq "Radiobutton" || [winfo class $w] eq "TRadiobutton"} {
	    set data [::tk::accessible::_getradiodata $w]
	    ::tk::accessible::acc_value $w $data
	    ::tk::accessible::emit_selection_change $w
	}
	if {[winfo class $w] eq "Checkbutton" || [winfo class $w] eq "TCheckbutton"} {
	    set data [::tk::accessible::_getcheckdata $w]
	    ::tk::accessible::acc_value $w $data
	    ::tk::accessible::emit_selection_change $w
	}
	    
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
	if {[winfo class $w] eq "Entry" || [winfo class $w] eq "TEntry"}  {
	    set data [::tk::accessible::_getentrytext $w]
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
	# Menu only needs to be tracked on X11 - native on Aqua and Windows
	if {[tk windowingsystem] eq "x11"} {
	    if {[winfo class $w] eq "Menu"} {
		set data [$w entrycget active -label]
		::tk::accessible::acc_value $w $data
		::tk::accessible::emit_selection_change $w
		::tk::accessible::emit_focus_change $w
	    }
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
	variable ::ttk::progressbar::Timers
	if {[info exists ::ttk::progressbar::Timers($w)]} {
	    return "busy"
	}
    }

    # Some widgets will not respond to keypress events unless
    # they have focus. Force Tk focus on the widget that currently has
    # accessibility focus if needed.

    proc _forceTkFocus {w} {
	# Check to make sure window is not destroyed. 
	if {[winfo exists $w]} {
	    if {[tk windowingsystem] eq "aqua"} {
		if {[winfo class $w] in {Scale TScale Spinbox TSpinbox Listbox Treeview TProgressbar}} {
		    if {[focus] ne $w} {
			focus -force $w
		    }
		}
	    } elseif {[tk windowingsystem] eq "win32"} {
		::tk::accessible::emit_focus_change $w
	    }
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
	if {[tk windowingsystem] ne "aqua"} {
	    # This is necessary to ensure correct accessible keyboard navigation
	    $w configure -takefocus 1
	}
	
	::tk::accessible::acc_role $w $role
	::tk::accessible::acc_name $w $name
	::tk::accessible::acc_description $w $description
	::tk::accessible::acc_value $w $value  
	::tk::accessible::acc_state $w $state
	::tk::accessible::acc_action $w $action
	
    }

    # Toplevel bindings.
    bind Toplevel <Map> {+::tk::accessible::_init \
			     %W \
			     Toplevel \
			     [wm title %W] \
			     {}  \
			     {} \
			     {} \
			     {} \
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
				 {%W invoke}\
			     }

    # Scale/TScale bindings.
    bind Scale <Map> {+::tk::accessible::_init \
			  %W \
			  Scale \
			  Scale \
			  Scale \
			  [%W get] \
			  [%W cget -state]\
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




    # Menu accessibility bindings for X11 only. Menus are native
    # on macOS/Windows, so we don’t expose them here.
    if {[tk windowingsystem] eq "x11"} {

	# Initialize the menu container itself when mapped.
	bind Menu <Map> {+
	    # Determine role based on whether this is a menubar
	    if {[winfo manager %W] eq "menubar"} {
		set role Menubar ;# ATK_ROLE_MENU_BAR
	    } else {
		set role Menu ;# ATK_ROLE_MENU
	    }

	    # Only fetch label if there is an active entry
	    set label ""
	    set idx [%W index active]
	    if {$idx ne ""} {
		set label [%W entrycget $idx -label]
	    }

	    ::tk::accessible::_init \
		       %W \
		       $role \
		       [winfo name %W] \
		       "" \
		       $label \
		       {} \
		       {}
	}

	# Initialize/update the currently active entry
	# whenever the selection changes.
	bind Menu <<MenuSelect>> {+
	    set idx [%W index active]
	    if {$idx ne ""} {
		set label [%W entrycget $idx -label]
		# Construct a unique ID for this entry under %W
		set entryId "%W:$idx"

		::tk::accessible::_init \
		    $entryId \
		    menuitem \       ;# ATK_ROLE_MENU_ITEM
                $label \
		    $label \
		    {} \
		    {} \
		    [list %W invoke $idx]

		# Update value and fire events
		::tk::accessible::acc_value $entryId $label
		::tk::accessible::emit_selection_change $entryId
		::tk::accessible::emit_focus_change $entryId
	    }
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
    bind Label <Map>  {+::tk::accessible::_init \
			   %W \
			   Label \
			   Label \
			   {} \
			   [%W cget -text] \
			   {}\
			   {}\
		       }

    bind TLabel <Map>    {+::tk::accessible::_init \
			      %W \
			      Label \
			      Label \
			      {} \
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
    
    bind all <Map> {+::tk::accessible::add_acc_object %W}
    
    # Various bindings to capture data/selection changes for
    # widgets that support returning a value. 

    # Selection changes.
    bind Listbox <<ListboxSelect>> {+::tk::accessible::_updateselection %W} 
    bind Treeview <<TreeviewSelect>> {+::tk::accessible::_updateselection %W}
    bind TCombobox <<ComboboxSelected>> {+::tk::accessible::_updateselection %W}
    bind Text <<Selection>> {+::tk::accessible::_updateselection %W}
    bind Radiobutton <<Invoke>> {+::tk::accessible::_updateselection %W}
	bind TRadiobutton <<Invoke>> {+::tk::accessible::_updateselection %W}
	bind Checkbutton <<Invoke>> {+::tk::accessible::_updateselection %W}
	bind TCheckbutton <<Invoke>> {+::tk::accessible::_updateselection %W}
    
    # Only need to track menu selection changes on X11.
    if {[tk windowingsystem] eq "x11"} {
	bind Menu <Up> {+
	    set current [%W index active]
	    if {$current eq ""} {
		set idx [%W index last]
	    } else {
		set idx [expr {$current - 1}]
	    }
	    set lastIndex [%W index last]
	    while {$idx >= 0} {
		if {[%W type $idx] ne "separator" && [%W entrycget $idx -state] ne "disabled"} {
		    %W activate $idx
		    ::tk::accessible::_updateselection %W
		    break
		}
		incr idx -1
	    }
	}
	bind Menu <Down> {+
	    set current [%W index active]
	    if {$current eq ""} {
		set idx 0
	    } else {
		set idx [expr {$current + 1}]
	    }
	    set lastIndex [%W index last]
	    while {$idx <= $lastIndex} {
		if {[%W type $idx] ne "separator" && [%W entrycget $idx -state] ne "disabled"} {
		    %W activate $idx
		    ::tk::accessible::_updateselection %W
		    break
		}
		incr idx
	    }
	}
	bind Menu <Return> {+
	    set idx [%W index active]
	    if {$idx ne "" && [%W type $idx] ne "separator" && [%W entrycget $idx -state] ne "disabled"} {
		%W invoke $idx
		::tk::accessible::_updateselection %W
	    }
	}
    }

    # Capture value changes from scale widgets.
    bind Scale <Right> {+::tk::accessible::_updatescale %W Right}
    bind Scale <Left> {+::tk::accessible::_updatescale %W Left}
    bind TScale <Right> {+::tk::accessible::_updatescale %W Right}
    bind TScale <Left> {+::tk::accessible::_updatescale %W Left}

    # In some contexts, the accessibility API is confused about widget
    # roles because of the way the widget is constructed. For instance,
    # VoiceOver and Orca misread the ttk::spinbox as an entry because
    # of how it is constructed. In such cases, let's re-use an old trick
    # that we used with the Aqua scrollbar when the ttk widgets were first
    # developed - map the ttk widget to its classic equivalent. There may
    # be a visual conflict but it is more important that the AT be able
    # to correclty identify teh widget and its value. 
    
    if {[tk windowingsystem] eq "aqua" || [tk windowingsystem] eq "x11"} {
	set result [::tk::accessible::check_screenreader]
	if {$result > 0} {
	    interp alias {} ::ttk::spinbox {} ::tk::spinbox
	}
    }
    if {[tk windowingsystem] eq "x11"} {
	set result [::tk::accessible::check_screenreader]
	if {$result > 0} {
	    interp alias {} ::ttk::radiobutton {} ::tk::radiobutton
	    interp alias {} ::ttk::checkbutton {} ::tk::checkbutton
	    interp alias {} ::ttk::scale {} ::tk::scale
	    
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

    # Capture text selection in entry widgets.
    bind Entry <KeyPress> {+::tk::accessible::_updateselection %W}
    bind TEntry <KeyPress> {+::tk::accessible::_updateselection %W}
    bind Entry <Left> {+::tk::accessible::_updateselection %W}
    bind TEntry <Left> {+::tk::accessible::_updateselection %W}
    bind Entry <Right> {+::tk::accessible::_updateselection %W}
    bind TEntry <Right> {+::tk::accessible::_updateselection %W}
    bind Entry <<Selection>> {+::tk::accessible::_updateselection %W}
    bind TEntry <<Selection>> {+::tk::accessible::_updateselection %W}

    # Progressbar updates.
    bind TProgressbar <FocusIn> {+::tk::accessible::_updateselection %W}

    bind Scrollbar <Up> {+%W set [expr {[%W get] - 0.1}]; ::tk::accessible::emit_selection_change %W}
    bind Scrollbar <Down> {+%W set [expr {[%W get] + 0.1}]; ::tk::accessible::emit_selection_change %W}
    bind TScrollbar <Up> {+%W set [expr {[%W get] - 0.1}]; ::tk::accessible::emit_selection_change %W}
    bind TScrollbar <Down> {+%W set [expr {[%W get] + 0.1}]; ::tk::accessible::emit_selection_change %W}

    bind Dialog <Return> {+::tk::dialog::OK %W; ::tk::accessible::emit_selection_change %W}
    bind Dialog <Escape> {+::tk::dialog::Cancel %W; ::tk::accessible::emit_selection_change %W}


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



