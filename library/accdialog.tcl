# accdialog.tcl --
#
#	Implements accessible messageboxes and file selection dialogs for X11.
#   Forked from msgbox.tcl and xmfbox.tcl. 
#
# Copyright © 1994-1997 Sun Microsystems, Inc.
# Copyright © 1998-2000 Scriptics Corporation
# Copyright © 2025 Kevin Walzer/WordTech Communications LLC
#
# See the file "license.terms" for information on usage and redistribution
# of this file, and for a DISCLAIMER OF ALL WARRANTIES.
#

# Ensure existence of ::tk::dialog namespace

if {[tk windowingsystem] eq "x11" && [::tk::accessible::check_screenreader] eq "1"} {

# Map relevant commands to ::tk::accessible namespace. 
    proc ::tk_getOpenFile {args} {
	return [::tk::accessible::FDialog open {*}args]
    }

    proc ::tk_getSaveFile {args} {
	    return [::tk::accessible::FDialog save {*}$args]
}

    proc ::tk_messageBox {args} {
	return [::tk::accessible::MessageBox {*}$args]
    }

namespace eval ::tk::dialog {}
namespace eval ::tk::dialog::file {}

# ::tk::accessible::MessageBox --
#
#	Pops up a messagebox with an application-supplied message with
#	an icon and a list of buttons. This procedure will be called
#	by tk_messageBox if the platform is X11 and is running a screen reader. 

#	Uses ::tk::Priv.${disp}(button) instead of ::tk::Priv(button) to
#	avoid adverse effects of [::tk::ScreenChanged]. Bug [e2cec2fa41].
#
#	This procedure is a private procedure shouldn't be called
#	directly. Call tk_messageBox instead.
#
#	See the user documentation for details on what tk_messageBox does.
#
proc ::tk::accessible::MessageBox {args} {

    variable ::tk::Priv

    set w ::tk::PrivMsgBox
    upvar $w data

    #
    # The default value of the title is space (" ") not the empty string
    # because for some window managers, a
    #		wm title .foo ""
    # causes the window title to be "foo" instead of the empty string.
    #
    set specs {
	{-default "" "" ""}
	{-detail "" "" ""}
	{-icon "" "" "info"}
	{-message "" "" ""}
	{-parent "" "" .}
	{-title "" "" " "}
	{-type "" "" "ok"}
    }

    tclParseConfigSpec $w $specs "" $args

    if {$data(-icon) ni {info warning error question}} {
	return -code error -errorcode [list TK LOOKUP ICON $data(-icon)] \
	    "bad -icon value \"$data(-icon)\": must be error, info, question, or warning"
    }
    set windowingsystem [tk windowingsystem]

    if {![winfo exists $data(-parent)]} {
	return -code error -errorcode [list TK LOOKUP WINDOW $data(-parent)] \
	    "bad window path name \"$data(-parent)\""
    }

    # Select the vwait variable carefully.
    set oldScreen $Priv(screen)
    set screen [winfo screen $data(-parent)]

    # Extract the display name (cf. ScreenChanged, including [Bug 2912473] fix).
    set disp [string range $screen 0 [string last . $screen]-1]

    # Ensure that namespace separators never occur in the display name (as
    # they cause problems in variable names). Double-colons exist in some VNC
    # display names. [Bug 2912473]
    set disp [string map {:: _doublecolon_} $disp]

    if {![info exists ::tk::Priv.${disp}]} {
	# Use ScreenChanged to create ::tk::Priv.${disp}, then change back to old
	# screen to avoid interfering with Tk expectations for bindings.
	ScreenChanged $screen
	ScreenChanged $oldScreen
    }

    variable ::tk::Priv.${disp}
    # Now in place of ::tk::Priv(button), use ::tk::Priv.${disp}(button) which
    # is the intended target variable of upvar and will not be redefined when
    # ::tk::ScreenChanged is called.

    switch -- $data(-type) {
	abortretryignore {
	    set names [list abort retry ignore]
	    set labels [list &Abort &Retry &Ignore]
	    set cancel abort
	}
	ok {
	    set names [list ok]
	    set labels {&OK}
	    set cancel ok
	}
	okcancel {
	    set names [list ok cancel]
	    set labels [list &OK &Cancel]
	    set cancel cancel
	}
	retrycancel {
	    set names [list retry cancel]
	    set labels [list &Retry &Cancel]
	    set cancel cancel
	}
	yesno {
	    set names [list yes no]
	    set labels [list &Yes &No]
	    set cancel no
	}
	yesnocancel {
	    set names [list yes no cancel]
	    set labels [list &Yes &No &Cancel]
	    set cancel cancel
	}
	default {
	    return -code error -errorcode [list TK LOOKUP DLG_TYPE $data(-type)] \
		"bad -type value \"$data(-type)\": must be\
		abortretryignore, ok, okcancel, retrycancel,\
		yesno, or yesnocancel"
	}
    }

    set buttons {}
    foreach name $names lab $labels {
	lappend buttons [list $name -text [mc $lab]]
    }

    # If no default button was specified, the default default is the
    # first button (Bug: 2218).

    if {$data(-default) eq ""} {
	set data(-default) [lindex [lindex $buttons 0] 0]
    }

    set valid 0
    foreach btn $buttons {
	if {[lindex $btn 0] eq $data(-default)} {
	    set valid 1
	    break
	}
    }
    if {!$valid} {
	return -code error -errorcode {TK MSGBOX DEFAULT} \
	    "bad -default value \"$data(-default)\": must be\
	    abort, retry, ignore, ok, cancel, no, or yes"
    }

    # 2. Set the dialog to be a child window of $parent
    #
    #
    if {$data(-parent) ne "."} {
	set w $data(-parent) .__accessible__messagebox
    } else {
	set w .__accessible__messagebox
    }

    # There is only one background colour for the whole dialog
    set bg [ttk::style lookup . -background]

    # 3. Create the top-level window and divide it into top
    # and bottom parts.

    catch {destroy $w}
    toplevel $w -class Dialog -bg $bg
    wm title $w $data(-title)
    wm iconname $w Dialog
    wm protocol $w WM_DELETE_WINDOW [list $w.$cancel invoke]

    # Message boxes should be transient with respect to their parent so that
    # they always stay on top of the parent window.  But some window managers
    # will simply create the child window as withdrawn if the parent is not
    # viewable (because it is withdrawn or iconified).  So only make the message box 
	# transient if the parent is viewable.
    #
    if {[winfo viewable [winfo toplevel $data(-parent)]] } {
	wm transient $w $data(-parent)
    }

	wm attributes $w -type dialog

    ttk::frame $w.bot
    grid anchor $w.bot center
    pack $w.bot -side bottom -fill both
    ttk::frame $w.top
    pack $w.top -side top -fill both -expand 1

    # 4. Fill the top part with bitmap, message and detail (use the
    # option database for -wraplength and -font so that they can be
    # overridden by the caller).

    option add *Dialog.msg.wrapLength 3i widgetDefault
    option add *Dialog.dtl.wrapLength 3i widgetDefault
    option add *Dialog.msg.font TkCaptionFont widgetDefault
    option add *Dialog.dtl.font TkDefaultFont widgetDefault

    ttk::label $w.msg -anchor nw -justify left -text $data(-message)
    if {$data(-detail) ne ""} {
	ttk::label $w.dtl -anchor nw -justify left -text $data(-detail)
    }
    if {$data(-icon) ne ""} {
	 switch $data(-icon) {
		error {
		    ttk::label $w.bitmap -image ::tk::icons::error
		}
		info {
		    ttk::label $w.bitmap -image ::tk::icons::information
		}
		question {
		    ttk::label $w.bitmap -image ::tk::icons::question
		}
		default {
		    ttk::label $w.bitmap -image ::tk::icons::warning
		}
	} 
    }
    grid $w.bitmap $w.msg -in $w.top -sticky news -padx 2m -pady 2m
    grid configure $w.bitmap -sticky nw
    grid columnconfigure $w.top 1 -weight 1
    if {$data(-detail) ne ""} {
	grid ^ $w.dtl -in $w.top -sticky news -padx 2m -pady {0 2m}
	grid rowconfigure $w.top 1 -weight 1
    } else {
	grid rowconfigure $w.top 0 -weight 1
    }

    # 5. Create a row of buttons at the bottom of the dialog.

    set i 0
    foreach but $buttons {
	set name [lindex $but 0]
	set opts [lrange $but 1 end]
	if {![llength $opts]} {
	    # Capitalize the first letter of $name
	    set capName [string toupper $name 0]
	    set opts [list -text $capName]
	}

	eval [list tk::AmpWidget ttk::button $w.$name] $opts \
		[list -command [list set tk::Priv.${disp}(button) $name]]

	if {$name eq $data(-default)} {
	    $w.$name configure -default active
	} else {
	    $w.$name configure -default normal
	}
	grid $w.$name -in $w.bot -row 0 -column $i -padx 3m -pady 2m -sticky ew
	grid columnconfigure $w.bot $i -uniform buttons
	
	incr i

	# create the binding for the key accelerator, based on the underline
	#
	# set underIdx [$w.$name cget -under]
	# if {$underIdx >= 0} {
	#     set key [string index [$w.$name cget -text] $underIdx]
	#     bind $w <Alt-[string tolower $key]>  [list $w.$name invoke]
	#     bind $w <Alt-[string toupper $key]>  [list $w.$name invoke]
	# }
    }
    bind $w <Alt-Key> [list ::tk::AltKeyInDialog $w %A]

    if {$data(-default) ne ""} {
	bind $w <FocusIn> {
	    if {[winfo class %W] in "Button TButton"} {
		%W configure -default active
	    }
	}
	bind $w <FocusOut> {
	    if {[winfo class %W] in "Button TButton"} {
		%W configure -default normal
	    }
	}
    }

    # 6. Create bindings for <Return>, <Escape> and <Destroy> on the dialog

    bind $w <Return> {
	if {[winfo class %W] in "Button TButton"} {
	    %W invoke
	}
    }

    # Invoke the designated cancelling operation
    bind $w <Escape> [list $w.$cancel invoke]

    # At <Destroy> the buttons have vanished, so must do this directly.
    bind $w.msg <Destroy> [list set tk::Priv.${disp}(button) $cancel]


    # 7. Withdraw the window, then update all the geometry information
    # so we know how big it wants to be, then center the window in the
    # display (::tk::accessible:: style) and de-iconify it.

    ::tk::PlaceWindow $w widget $data(-parent)

    # 8. Claim the focus too. Do not set a grab because grabs do not play nicely
	# with the GLib event loop. 

    if {$data(-default) ne ""} {
	set focus $w.$data(-default)
    } else {
	set focus $w
    }
   
    # 9. Wait for the user to respond, then restore the focus and
    # return the index of the selected button.  Restore the focus
    # before deleting the window, since otherwise the window manager
    # may take the focus away so we can't redirect it.  

    vwait ::tk::Priv.${disp}(button)
    # Copy the result now so any <Destroy> that happens won't cause
    # trouble
    set result [set Priv.${disp}(button)]

    return $result
}


# ::tk::accessible::FDialog  --
#
#	Implements an accessible file selection dialog for the Unix platform. 
#   Based on the ::tk::accessible::-style dialog that is used if ::tk_strictMotif is set to 1.
#   This dialog is suitable for accessibility because it is built on standard
#   accessible Tk widgets (listbox, entry, scrollbar, etc). The standard Unix dialogs
#   are not suitable because they are built with the canvas widget, which is not
#   supported in the ::tk::accessibility API. 
#

proc ::tk::accessible::FDialog {type args} {
    variable ::tk::Priv
    set dataName __tk_filedialog
    upvar ::tk::dialog::file::$dataName data

    set w [::tk::accessible::FDialog_Create $dataName $type $args]

    # Set a grab and claim the focus too.

	wm deiconify $w; raise $w; focus -force $w
    $data(sEnt) selection range 0 end

    # Wait for the user to respond, then restore the focus and
    # return the index of the selected button.  Restore the focus
    # before deleting the window, since otherwise the window manager
    # may take the focus away so we can't redirect it.

    vwait ::tk::Priv(selectFilePath)
    set result $Priv(selectFilePath)
	wm withdraw $w

    return $result
}

# ::tk::accessible::FDialog_Create --
#
#	Creates the accessible file dialog (if it doesn't exist yet) and
#	initialize the internal data structure associated with the
#	dialog.
#
#	This procedure is used by ::tk::accessible::FDialog to create the
#	dialog.
#
# Arguments:
#	dataName	Name of the global "data" array for the file dialog.
#	type		"Save" or "Open"
#	argList		Options parsed by the procedure.
#
# Results:
#	Pathname of the file dialog.

proc ::tk::accessible::FDialog_Create {dataName type argList} {
    upvar ::tk::dialog::file::$dataName data

    ::tk::accessible::FDialog_Config $dataName $type $argList

    if {$data(-parent) eq "."} {
	set w .$dataName
    } else {
	set w $data(-parent).$dataName
    }

    # (re)create the dialog box if necessary
    #
    if {![winfo exists $w]} {
	::tk::accessible::FDialog_BuildUI $w
   } elseif {[winfo class $w] ne "TkMotifFDialog"} {
	destroy $w
	::tk::accessible::FDialog_BuildUI $w
    } else {
	set data(fEnt) $w.top.f1.ent
	set data(dList) $w.top.f2.a.l
	set data(fList) $w.top.f2.b.l
	set data(sEnt) $w.top.f3.ent
	set data(okBtn) $w.bot.ok
	set data(filterBtn) $w.bot.filter
	set data(cancelBtn) $w.bot.cancel
    }
    ::tk::accessible::FDialog_SetListMode $w

    # Dialog boxes should be transient with respect to their parent,
    # so that they will always stay on top of their parent window.  However,
    # some window managers will create the window as withdrawn if the parent
    # window is withdrawn or iconified.  Combined with the grab we put on the
    # window, this can hang the entire application.  Therefore we only make
    # the dialog transient if the parent is viewable.

    if {[winfo viewable [winfo toplevel $data(-parent)]] } {
	wm transient $w $data(-parent)
    }

    ::tk::accessible::FDialog_FileTypes $w
    ::tk::accessible::FDialog_Update $w

    # Withdraw the window, then update all the geometry information
    # so we know how big it wants to be, then center the window in the
    # display (::tk::accessible:: style) and de-iconify it.

    ::tk::PlaceWindow $w
    wm title $w $data(-title)

    return $w
}

# ::tk::accessible::FDialog_FileTypes --
#
#	Checks the -filetypes option. If present this adds a list of radio-
#	buttons to pick the file types from.
#
# Arguments:
#	w		Pathname of the tk_get*File dialogue.
#
# Results:
#	none

proc ::tk::accessible::FDialog_FileTypes {w} {
    upvar ::tk::dialog::file::[winfo name $w] data

    set f $w.top.f3.types
    destroy $f

    # No file types: use "*" as the filter and display no radio-buttons
    if {$data(-filetypes) eq ""} {
	set data(filter) *
	return
    }

    # The filetypes radiobuttons
    # set data(fileType) $data(-defaulttype)
    # Default type to first entry
    set initialTypeName [lindex $data(origfiletypes) 0 0]
    if {$data(-typevariable) ne ""} {
	upvar #0 $data(-typevariable) typeVariable
	if {[info exists typeVariable]} {
	    set initialTypeName $typeVariable
	}
    }
    set ix 0
    set data(fileType) 0
    foreach fltr $data(origfiletypes) {
	set fname [lindex $fltr 0]
	if {[string first $initialTypeName $fname] == 0} {
	    set data(fileType) $ix
	    break
	}
	incr ix
    }

    ::tk::accessible::FDialog_SetFilter $w [lindex $data(-filetypes) $data(fileType)]

    #don't produce radiobuttons for only one filetype
    if {[llength $data(-filetypes)] == 1} {
	return
    }

    frame $f
    set cnt 0
    if {$data(-filetypes) ne {}} {
	foreach type $data(-filetypes) {
	    set title  [lindex $type 0]
	    set filter [lindex $type 1]
	    radiobutton $f.b$cnt \
		-text $title \
		-variable ::tk::dialog::file::[winfo name $w](fileType) \
		-value $cnt \
		-command [list ::tk::accessible::FDialog_SetFilter $w $type]
	    pack $f.b$cnt -side left
	    incr cnt
	}
    }
    $f.b$data(fileType) invoke

    pack $f -side bottom -fill both

    return
}

# This proc gets called whenever data(filter) is set
#
proc ::tk::accessible::FDialog_SetFilter {w type} {
    upvar ::tk::dialog::file::[winfo name $w] data
    variable ::tk::Priv

    set data(filter) [lindex $type 1]

    ::tk::accessible::FDialog_Update $w
}

# ::tk::accessible::FDialog_Config --
#
#	Iterates over the optional arguments to determine the option
#	values for the ::tk::accessible:: file dialog; gives default values to
#	unspecified options.
#
# Arguments:
#	dataName	The name of the global variable in which
#			data for the file dialog is stored.
#	type		"Save" or "Open"
#	argList		Options parsed by the procedure.

proc ::tk::accessible::FDialog_Config {dataName type argList} {
    upvar ::tk::dialog::file::$dataName data

    set data(type) $type

    # 1: the configuration specs
    #
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
    }
    if {$type eq "save"} {
	lappend specs {-confirmoverwrite "" "" "1"}
    }

    set data(-multiple) 0
    set data(-confirmoverwrite) 1
    # 2: default values depending on the type of the dialog
    #
    if {![info exists data(selectPath)]} {
	# first time the dialog has been popped up
	set data(selectPath) [pwd]
	set data(selectFile) ""
    }

    # 3: parse the arguments
    #
    tclParseConfigSpec ::tk::dialog::file::$dataName $specs "" $argList

    if {$data(-title) eq ""} {
	if {$type eq "open"} {
	    if {$data(-multiple) != 0} {
		set data(-title) "[mc {Open Multiple Files}]"
	    } else {
		set data(-title) [mc "Open"]
	    }
	} else {
	    set data(-title) [mc "Save As"]
	}
    }

    # 4: set the default directory and selection according to the -initial
    #    settings
    #
    if {$data(-initialdir) ne ""} {
	if {[file isdirectory $data(-initialdir)]} {
	    set data(selectPath) [lindex [glob $data(-initialdir)] 0]
	} else {
	    set data(selectPath) [pwd]
	}

	# Convert the initialdir to an absolute path name.

	set old [pwd]
	cd $data(selectPath)
	set data(selectPath) [pwd]
	cd $old
    }
    set data(selectFile) $data(-initialfile)

    # 5. Parse the -filetypes option. It is not used by the ::tk::accessible::
    #    file dialog, but we check for validity of the value to make sure
    #    the application code also runs fine with the TK file dialog.
    #
    set data(origfiletypes) $data(-filetypes)
    set data(-filetypes) [::tk::FDGetFileTypes $data(-filetypes)]

    if {![info exists data(filter)]} {
	set data(filter) *
    }
    if {![winfo exists $data(-parent)]} {
	return -code error -errorcode [list TK LOOKUP WINDOW $data(-parent)] \
	    "bad window path name \"$data(-parent)\""
    }
}

# ::tk::accessible::FDialog_BuildUI --
#
#	Builds the UI components of the ::tk::accessible:: file dialog.
#
# Arguments:
#	w		Pathname of the dialog to build.
#
# Results:
#	None.

proc ::tk::accessible::FDialog_BuildUI {w} {
    set dataName [lindex [split $w .] end]
    upvar ::tk::dialog::file::$dataName data

    # Create the dialog toplevel and internal frames.
    #
    toplevel $w -class Tk::tk::accessible::FDialog
    set top [frame $w.top -relief raised -bd 1]
    set bot [frame $w.bot -relief raised -bd 1]

    pack $w.bot -side bottom -fill x
    pack $w.top -side top -expand yes -fill both

    set f1 [frame $top.f1]
    set f2 [frame $top.f2]
    set f3 [frame $top.f3]

    pack $f1 -side top    -fill x
    pack $f3 -side bottom -fill x
    pack $f2 -expand yes -fill both

    set f2a [frame $f2.a]
    set f2b [frame $f2.b]

    grid $f2a -row 0 -column 0 -rowspan 1 -columnspan 1 -padx 3p -pady 3p \
	-sticky news
    grid $f2b -row 0 -column 1 -rowspan 1 -columnspan 1 -padx 3p -pady 3p \
	-sticky news
    grid rowconfigure $f2 0    -minsize 0   -weight 1
    grid columnconfigure $f2 0 -minsize 0   -weight 1
    grid columnconfigure $f2 1 -minsize 150 -weight 2

    # The Filter box
    #
    bind [::tk::AmpWidget label $f1.lab -text [mc "Fil&ter:"] -anchor w] \
	<<AltUnderlined>> [list focus $f1.ent]
    entry $f1.ent
    pack $f1.lab -side top -fill x -padx 4.5p -pady 3p
    pack $f1.ent -side top -fill x -padx 3p -pady 0
    set data(fEnt) $f1.ent

    # The file and directory lists
    #
    set data(dList) [::tk::accessible::FDialog_MakeSList $w $f2a \
	    [mc "&Directory:"] DList]
    set data(fList) [::tk::accessible::FDialog_MakeSList $w $f2b \
	    [mc "Fi&les:"]     FList]

    # The Selection box
    #
    bind [::tk::AmpWidget label $f3.lab -text [mc "&Selection:"] -anchor w] \
	<<AltUnderlined>> [list focus $f3.ent]
    entry $f3.ent
    pack $f3.lab -side top -fill x -padx 4.5p -pady 0
    pack $f3.ent -side top -fill x -padx 3p -pady 3p
    set data(sEnt) $f3.ent

    # The buttons
    #
    set maxWidth [::tk::mcmaxamp &OK &Filter &Cancel]
    set maxWidth [expr {$maxWidth<6?6:$maxWidth}]
    set data(okBtn) [::tk::AmpWidget button $bot.ok -text [mc "&OK"] \
	    -width $maxWidth \
	    -command [list ::tk::accessible::FDialog_OkCmd $w]]
    set data(filterBtn) [::tk::AmpWidget button $bot.filter -text [mc "&Filter"] \
	    -width $maxWidth \
	    -command [list ::tk::accessible::FDialog_FilterCmd $w]]
    set data(cancelBtn) [::tk::AmpWidget button $bot.cancel -text [mc "&Cancel"] \
	    -width $maxWidth \
	    -command [list ::tk::accessible::FDialog_CancelCmd $w]]

    pack $bot.ok $bot.filter $bot.cancel -padx 7.5p -pady 7.5p -expand yes \
	-side left

    # Create the bindings:
    #
    bind $w <Alt-Key> [list ::tk::AltKeyInDialog $w %A]

    bind $data(fEnt) <Return> [list ::tk::accessible::FDialog_ActivateFEnt $w]
    bind $data(sEnt) <Return> [list ::tk::accessible::FDialog_ActivateSEnt $w]
    bind $w <Escape> [list ::tk::accessible::FDialog_CancelCmd $w]
    bind $w.bot <Destroy> {set ::tk::Priv(selectFilePath) {}}

    wm protocol $w WM_DELETE_WINDOW [list ::tk::accessible::FDialog_CancelCmd $w]
}

proc ::tk::accessible::FDialog_SetListMode {w} {
    upvar ::tk::dialog::file::[winfo name $w] data

    if {$data(-multiple) != 0} {
	set selectmode extended
    } else {
	set selectmode browse
    }
    set f $w.top.f2.b
    $f.l configure -selectmode $selectmode
}

# ::tk::accessible::FDialog_MakeSList --
#
#	Create a scrolled-listbox and set the keyboard accelerator
#	bindings so that the list selection follows what the user
#	types.
#
# Arguments:
#	w		Pathname of the dialog box.
#	f		Frame widget inside which to create the scrolled
#			listbox. This frame widget already exists.
#	label		The string to display on top of the listbox.
#	under		Sets the -under option of the label.
#	cmdPrefix	Specifies procedures to call when the listbox is
#			browsed or activated.

proc ::tk::accessible::FDialog_MakeSList {w f label cmdPrefix} {
    bind [::tk::AmpWidget label $f.lab -text $label -anchor w] \
	<<AltUnderlined>> [list focus $f.l]
    listbox $f.l -width 12 -height 5 -exportselection 0\
	-xscrollcommand [list $f.h set]	-yscrollcommand [list $f.v set]
    scrollbar $f.v -orient vertical   -takefocus 0 -command [list $f.l yview]
    scrollbar $f.h -orient horizontal -takefocus 0 -command [list $f.l xview]
    grid $f.lab -row 0 -column 0 -sticky news -rowspan 1 -columnspan 2 \
	-padx 1.5p -pady 1.5p
    grid $f.l -row 1 -column 0 -rowspan 1 -columnspan 1 -sticky news
    grid $f.v -row 1 -column 1 -rowspan 1 -columnspan 1 -sticky news
    grid $f.h -row 2 -column 0 -rowspan 1 -columnspan 1 -sticky news

    grid rowconfigure    $f 0 -weight 0 -minsize 0
    grid rowconfigure    $f 1 -weight 1 -minsize 0
    grid columnconfigure $f 0 -weight 1 -minsize 0

    # bindings for the listboxes
    #
    set list $f.l
    bind $list <<ListboxSelect>> [list ::tk::accessible::FDialog_Browse$cmdPrefix $w]
    bind $list <Double-ButtonRelease-1> \
	    [list ::tk::accessible::FDialog_Activate$cmdPrefix $w]
    bind $list <Return>	"::tk::accessible::FDialog_Browse$cmdPrefix [list $w]; \
	    ::tk::accessible::FDialog_Activate$cmdPrefix [list $w]"

    bindtags $list [list Listbox $list [winfo toplevel $list] all]
    ListBoxKeyAccel_Set $list

    return $f.l
}

# ::tk::accessible::FDialog_InterpFilter --
#
#	Interpret the string in the filter entry into two components:
#	the directory and the pattern. If the string is a relative
#	pathname, give a warning to the user and restore the pattern
#	to original.
#
# Arguments:
#	w		pathname of the dialog box.
#
# Results:
#	A list of two elements. The first element is the directory
#	specified # by the filter. The second element is the filter
#	pattern itself.

proc ::tk::accessible::FDialog_InterpFilter {w} {
    upvar ::tk::dialog::file::[winfo name $w] data

    set text [string trim [$data(fEnt) get]]

    # Perform tilde substitution
    #
    set badTilde 0
    if {[string index $text 0] eq "~"} {
	set list [file split $text]
	set tilde [lindex $list 0]
	if {[catch {set tilde [glob $tilde]}]} {
	    set badTilde 1
	} else {
	    set text [eval file join [concat $tilde [lrange $list 1 end]]]
	}
    }

    # If the string is a relative pathname, combine it
    # with the current selectPath.

    set relative 0
    if {[file pathtype $text] eq "relative"} {
	set relative 1
    } elseif {$badTilde} {
	set relative 1
    }

    if {$relative} {
	tk_messageBox -icon warning -type ok \
		-message "\"$text\" must be an absolute pathname"

	$data(fEnt) delete 0 end
	$data(fEnt) insert 0 [::tk::dialog::file::JoinFile $data(selectPath) \
		$data(filter)]

	return [list $data(selectPath) $data(filter)]
    }

    set resolved [::tk::dialog::file::JoinFile [file dirname $text] [file tail $text]]

    if {[file isdirectory $resolved]} {
	set dir $resolved
	set fil $data(filter)
    } else {
	set dir [file dirname $resolved]
	set fil [file tail    $resolved]
    }

    return [list $dir $fil]
}

# ::tk::accessible::FDialog_Update
#
#	Load the files and synchronize the "filter" and "selection" fields
#	boxes.
#
# Arguments:
#	w		pathname of the dialog box.
#
# Results:
#	None.

proc ::tk::accessible::FDialog_Update {w} {
    upvar ::tk::dialog::file::[winfo name $w] data

    $data(fEnt) delete 0 end
    $data(fEnt) insert 0 \
	    [::tk::dialog::file::JoinFile $data(selectPath) $data(filter)]
    $data(sEnt) delete 0 end
    $data(sEnt) insert 0 [::tk::dialog::file::JoinFile $data(selectPath) \
	    $data(selectFile)]

    ::tk::accessible::FDialog_LoadFiles $w
}

# ::tk::accessible::FDialog_LoadFiles --
#
#	Loads the files and directories into the two listboxes according
#	to the filter setting.
#
# Arguments:
#	w		pathname of the dialog box.
#
# Results:
#	None.

proc ::tk::accessible::FDialog_LoadFiles {w} {
    upvar ::tk::dialog::file::[winfo name $w] data

    $data(dList) delete 0 end
    $data(fList) delete 0 end

    set appPWD [pwd]
    if {[catch {cd $data(selectPath)}]} {
	cd $appPWD

	$data(dList) insert end ".."
	return
    }

    # Make the dir and file lists
    #
    # For speed we only have one glob, which reduces the file system
    # calls (good for slow NFS networks).
    #
    # We also do two smaller sorts (files + dirs) instead of one large sort,
    # which gives a small speed increase.
    #
    set top 0
    set dlist ""
    set flist ""
    foreach f [glob -nocomplain .* *] {
	if {[file isdir ./$f]} {
	    lappend dlist $f
	} else {
	    foreach pat $data(filter) {
		if {[string match $pat $f]} {
		    if {[string match .* $f]} {
			incr top
		    }
		    lappend flist $f
		    break
		}
	    }
	}
    }
    eval [list $data(dList) insert end] [lsort -dictionary $dlist]
    eval [list $data(fList) insert end] [lsort -dictionary $flist]

    # The user probably doesn't want to see the . files. We adjust the view
    # so that the listbox displays all the non-dot files
    $data(fList) yview $top

    cd $appPWD
}

# ::tk::accessible::FDialog_BrowseDList --
#
#	This procedure is called when the directory list is browsed
#	(clicked-over) by the user.
#
# Arguments:
#	w		The pathname of the dialog box.
#
# Results:
#	None.

proc ::tk::accessible::FDialog_BrowseDList {w} {
    upvar ::tk::dialog::file::[winfo name $w] data

    focus $data(dList)
    if {[$data(dList) curselection] eq ""} {
	return
    }
    set subdir [$data(dList) get [$data(dList) curselection]]
    if {$subdir eq ""} {
	return
    }

    $data(fList) selection clear 0 end

    set list [::tk::accessible::FDialog_InterpFilter $w]
    set data(filter) [lindex $list 1]

    switch -- $subdir {
	. {
	    set newSpec [::tk::dialog::file::JoinFile $data(selectPath) $data(filter)]
	}
	.. {
	    set newSpec [::tk::dialog::file::JoinFile [file dirname $data(selectPath)] \
		$data(filter)]
	}
	default {
	    set newSpec [::tk::dialog::file::JoinFile [::tk::dialog::file::JoinFile \
		    $data(selectPath) $subdir] $data(filter)]
	}
    }

    $data(fEnt) delete 0 end
    $data(fEnt) insert 0 $newSpec
}

# ::tk::accessible::FDialog_ActivateDList --
#
#	This procedure is called when the directory list is activated
#	(double-clicked) by the user.
#
# Arguments:
#	w		The pathname of the dialog box.
#
# Results:
#	None.

proc ::tk::accessible::FDialog_ActivateDList {w} {
    upvar ::tk::dialog::file::[winfo name $w] data

    if {[$data(dList) curselection] eq ""} {
	return
    }
    set subdir [$data(dList) get [$data(dList) curselection]]
    if {$subdir eq ""} {
	return
    }

    $data(fList) selection clear 0 end

    switch -- $subdir {
	. {
	    set newDir $data(selectPath)
	}
	.. {
	    set newDir [file dirname $data(selectPath)]
	}
	default {
	    set newDir [::tk::dialog::file::JoinFile $data(selectPath) $subdir]
	}
    }

    set data(selectPath) $newDir
    ::tk::accessible::FDialog_Update $w

    if {$subdir ne ".."} {
	$data(dList) selection set 0
	$data(dList) activate 0
    } else {
	$data(dList) selection set 1
	$data(dList) activate 1
    }
}

# ::tk::accessible::FDialog_BrowseFList --
#
#	This procedure is called when the file list is browsed
#	(clicked-over) by the user.
#
# Arguments:
#	w		The pathname of the dialog box.
#
# Results:
#	None.

proc ::tk::accessible::FDialog_BrowseFList {w} {
    upvar ::tk::dialog::file::[winfo name $w] data

    focus $data(fList)
    set data(selectFile) ""
    foreach item [$data(fList) curselection] {
	lappend data(selectFile) [$data(fList) get $item]
    }
    if {[llength $data(selectFile)] == 0} {
	return
    }

    $data(dList) selection clear 0 end

    $data(fEnt) delete 0 end
    $data(fEnt) insert 0 [::tk::dialog::file::JoinFile $data(selectPath) \
	    $data(filter)]
    $data(fEnt) xview end

    # if it's a multiple selection box, just put in the filenames
    # otherwise put in the full path as usual
    $data(sEnt) delete 0 end
    if {$data(-multiple) != 0} {
	$data(sEnt) insert 0 $data(selectFile)
    } else {
	$data(sEnt) insert 0 [::tk::dialog::file::JoinFile $data(selectPath) \
		[lindex $data(selectFile) 0]]
    }
    $data(sEnt) xview end
}

# ::tk::accessible::FDialog_ActivateFList --
#
#	This procedure is called when the file list is activated
#	(double-clicked) by the user.
#
# Arguments:
#	w		The pathname of the dialog box.
#
# Results:
#	None.

proc ::tk::accessible::FDialog_ActivateFList {w} {
    upvar ::tk::dialog::file::[winfo name $w] data

    if {[$data(fList) curselection] eq ""} {
	return
    }
    set data(selectFile) [$data(fList) get [$data(fList) curselection]]
    if {$data(selectFile) eq ""} {
	return
    } else {
	::tk::accessible::FDialog_ActivateSEnt $w
    }
}

# ::tk::accessible::FDialog_ActivateFEnt --
#
#	This procedure is called when the user presses Return inside
#	the "filter" entry. It updates the dialog according to the
#	text inside the filter entry.
#
# Arguments:
#	w		The pathname of the dialog box.
#
# Results:
#	None.

proc ::tk::accessible::FDialog_ActivateFEnt {w} {
    upvar ::tk::dialog::file::[winfo name $w] data

    set list [::tk::accessible::FDialog_InterpFilter $w]
    set data(selectPath) [lindex $list 0]
    set data(filter)    [lindex $list 1]

    ::tk::accessible::FDialog_Update $w
}

# ::tk::accessible::FDialog_ActivateSEnt --
#
#	This procedure is called when the user presses Return inside
#	the "selection" entry. It sets the ::tk::Priv(selectFilePath)
#	variable so that the vwait loop in ::tk::accessible::FDialog will be
#	terminated.
#
# Arguments:
#	w		The pathname of the dialog box.
#
# Results:
#	None.

proc ::tk::accessible::FDialog_ActivateSEnt {w} {
    variable ::tk::Priv
    upvar ::tk::dialog::file::[winfo name $w] data

    set selectFilePath [string trim [$data(sEnt) get]]

    if {$selectFilePath eq ""} {
	::tk::accessible::FDialog_FilterCmd $w
	return
    }

    if {$data(-multiple) == 0} {
	set selectFilePath [list $selectFilePath]
    }

    if {[file isdirectory [lindex $selectFilePath 0]]} {
	set data(selectPath) [lindex [glob $selectFilePath] 0]
	set data(selectFile) ""
	::tk::accessible::FDialog_Update $w
	return
    }

    set newFileList ""
    foreach item $selectFilePath {
	if {[file pathtype $item] ne "absolute"} {
	    set item [file join $data(selectPath) $item]
	} elseif {![file exists [file dirname $item]]} {
	    tk_messageBox -icon warning -type ok \
		    -message [mc {Directory "%1$s" does not exist.} \
		    [file dirname $item]]
	    return
	}

	if {![file exists $item]} {
	    if {$data(type) eq "open"} {
		tk_messageBox -icon warning -type ok \
			-message [mc {File "%1$s" does not exist.} $item]
		return
	    }
	} elseif {$data(type) eq "save" && $data(-confirmoverwrite)} {
	    set message [format %s%s \
		    [mc "File \"%1\$s\" already exists.\n\n" $selectFilePath] \
		    [mc {Replace existing file?}]]
	    set answer [tk_messageBox -icon warning -type yesno \
		    -message $message]
	    if {$answer eq "no"} {
		return
	    }
	}

	lappend newFileList $item
    }

    # Return selected filter
    if {[info exists data(-typevariable)] && $data(-typevariable) ne ""
	    && [info exists data(-filetypes)] && $data(-filetypes) ne ""} {
	upvar #0 $data(-typevariable) typeVariable
	set typeVariable [lindex $data(origfiletypes) $data(fileType) 0]
    }

    if {$data(-multiple) != 0} {
	set Priv(selectFilePath) $newFileList
    } else {
	set Priv(selectFilePath) [lindex $newFileList 0]
    }

    # Set selectFile and selectPath to first item in list
    set Priv(selectFile)     [file tail    [lindex $newFileList 0]]
    set Priv(selectPath)     [file dirname [lindex $newFileList 0]]
}


proc ::tk::accessible::FDialog_OkCmd {w} {
    upvar ::tk::dialog::file::[winfo name $w] data

    ::tk::accessible::FDialog_ActivateSEnt $w
}

proc ::tk::accessible::FDialog_FilterCmd {w} {
    upvar ::tk::dialog::file::[winfo name $w] data

    ::tk::accessible::FDialog_ActivateFEnt $w
}

proc ::tk::accessible::FDialog_CancelCmd {w} {
    variable ::tk::Priv

    set Priv(selectFilePath) ""
    set Priv(selectFile)     ""
    set Priv(selectPath)     ""
}

proc ::tk::ListBoxKeyAccel_Set {w} {
    bind Listbox <Key> ""
    bind $w <Destroy> [list tk::ListBoxKeyAccel_Unset $w]
    bind $w <Key> [list tk::ListBoxKeyAccel_Key $w %A]
}

proc ::tk::ListBoxKeyAccel_Unset {w} {
    variable ::tk::Priv

    catch {after cancel $Priv(lbAccel,$w,afterId)}
    unset -nocomplain Priv(lbAccel,$w) Priv(lbAccel,$w,afterId)
}

# ::tk::ListBoxKeyAccel_Key--
#
#	This procedure maintains a list of recently entered keystrokes
#	over a listbox widget. It arranges an idle event to move the
#	selection of the listbox to the entry that begins with the
#	keystrokes.
#
# Arguments:
#	w		The pathname of the listbox.
#	key		The key which the user just pressed.
#
# Results:
#	None.

proc ::tk::ListBoxKeyAccel_Key {w key} {
    variable ::tk::Priv

    if { $key eq "" } {
	return
    }
    append Priv(lbAccel,$w) $key
    ListBoxKeyAccel_Goto $w $Priv(lbAccel,$w)
    catch {
	after cancel $Priv(lbAccel,$w,afterId)
    }
    set Priv(lbAccel,$w,afterId) [after 500 \
	    [list tk::ListBoxKeyAccel_Reset $w]]
}

proc ::tk::ListBoxKeyAccel_Goto {w string} {
    variable ::tk::Priv

    set string [string tolower $string]
    set end [$w index end]
    set theIndex -1

    for {set i 0} {$i < $end} {incr i} {
	set item [string tolower [$w get $i]]
	if {[string compare $string $item] >= 0} {
	    set theIndex $i
	}
	if {[string compare $string $item] <= 0} {
	    set theIndex $i
	    break
	}
    }

    if {$theIndex >= 0} {
	$w selection clear 0 end
	$w selection set $theIndex $theIndex
	$w activate $theIndex
	$w see $theIndex
	event generate $w <<ListboxSelect>>
    }
}

proc ::tk::ListBoxKeyAccel_Reset {w} {
    variable ::tk::Priv

    unset -nocomplain Priv(lbAccel,$w)
}

