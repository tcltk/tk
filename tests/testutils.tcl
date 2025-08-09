# testutils.tcl --
#
# This file is sourced by each test file when invoking "tcltest::loadTestedCommands".
# It implements the testutils mechanism which is used to import utility procs
# into test files that need them.
#
# See the file "license.terms" for information on usage and redistribution
# of this file, and for a DISCLAIMER OF ALL WARRANTIES.

#
# DOCUMENTATION FOR TEST AUTHORS AND MAINTAINERS
#
# The testutils mechanism is documented in the separate file "testutils.GUIDE",
# which is placed in the same directory as this file "testutils.tcl".
#

namespace eval ::tk::test {
    #
    # The namespace ::tk::test itself doesn't contain any procs or variables.
    # The contents of this namespace exist solely in child namespaces that
    # are defined hereafter.
    #
    # Each child namespace represents a functional area, also called "domain".
    #
}


namespace eval ::tk::test::generic {

    proc assert {expr} {
	if {! [uplevel 1 [list expr $expr]]} {
	    return -code error "assertion failed: \"[uplevel 1 [list subst -nocommands $expr]]\""
	}
    }

    # controlPointerWarpTiming --
    #
    # This proc is intended to ensure that the (mouse) pointer has actually
    # been moved to its new position after a Tk test issued:
    #
    #    [event generate $w $event -warp 1 ...]
    #
    # It takes care of the following timing details of pointer warping:
    #
    # a. Allow pointer warping to happen if it was scheduled for execution at
    #    idle time. This happens synchronously if $w refers to the
    #    whole screen or if the -when option to [event generate] is "now".
    #
    # b. Work around a race condition associated with OS notification of
    #    mouse motion on Windows.
    #
    #    When calling [event generate $w $event -warp 1 ...], the following
    #    sequence occurs:
    #    - At some point in the processing of this command, either via a
    #      synchronous execution path, or asynchronously at idle time, Tk calls
    #      an OS function* to carry out the mouse cursor motion.
    #    - Tk has previously registered a callback function** with the OS, for
    #      the OS to call in order to notify Tk when a mouse move is completed.
    #    - Tk doesn't wait for the callback function to receive the notification
    #      from the OS, but continues processing. This suits most use cases
    #      because usually the notification arrives fast enough (within a few tens
    #      of microseconds). However ...
    #    - A problem arises if Tk performs some processing, immediately following
    #      up on [event generate $w $event -warp 1 ...], and that processing
    #      relies on the mouse pointer having actually moved. If such processing
    #      happens just before the notification from the OS has been received,
    #      Tk will be using not yet updated info (e.g. mouse coordinates).
    #
    #         Hickup, choke etc ... !
    #
    #            *  the function SendInput() of the Win32 API
    #            ** the callback function is TkWinChildProc()
    #
    #    This timing issue can be addressed by putting the Tk process on hold
    #    (do nothing at all) for a somewhat extended amount of time, while
    #    letting the OS complete its job in the meantime. This is what is
    #    accomplished by calling [after ms].
    #
    #    ----
    #    For the history of this issue please refer to Tk ticket [69b48f427e],
    #    specifically the comment on 2019-10-27 14:24:26.
    #
    #
    # Beware: there are cases, not (yet) exercised by the Tk test suite, where
    # [controlPointerWarpTiming] doesn't ensure the new position of the pointer.
    # For example, when issued under Tk8.7+, if the value for the -when option
    # to [event generate $w] is not "now", and $w refers to a Tk window, i.e. not
    # the whole screen.
    #
    proc controlPointerWarpTiming {{duration 50}} {
	update idletasks ;# see a. above
	if {[tk windowingsystem] eq "win32"} {
	    after $duration ;# see b. above
	}
    }

    proc deleteWindows {} {
	destroy {*}[winfo children .]
	# This update is needed to avoid intermittent failures on macOS in unixEmbed.test
	# with the (GitHub Actions) CI runner.
	# Reason for the failures is unclear but could have to do with window ids being deleted
	# after the destroy command returns. The detailed mechanism of such delayed deletions
	# is not understood, but it appears that this update prevents the test failures.
	update
    }

    proc fixfocus {} {
	catch {destroy .focus}
	toplevel .focus
	wm geometry .focus +0+0
	entry .focus.e
	.focus.e insert 0 "fixfocus"
	pack .focus.e
	update
	focus -force .focus.e
	destroy .focus
    }

    proc loadTkCommand {} {
	variable TkLoadCmd
	if {! [info exists TkLoadCmd]} {
	    foreach pkg [info loaded] {
		if {[lindex $pkg 1] eq "Tk"} {
		    set TkLoadCmd [list load {*}$pkg]
		    break
		}
	    }
	}
	return $TkLoadCmd
    }

    # Suspend script execution for a given amount of time, but continue
    # processing events.
    proc pause {ms} {
	variable _pause

	set num [incr _pause(count)]
	set _pause($num) 1

	after $ms [list unset [namespace current]::_pause($num)]
	vwait [namespace current]::_pause($num)
    }

    # On macOS windows are not allowed to overlap the menubar at the top of the
    # screen or the dock.  So tests which move a window and then check whether it
    # got moved to the requested location should use a y coordinate larger than the
    # height of the menubar (normally 23 pixels) and an x coordinate larger than the
    # width of the dock, if it happens to be on the left.
    # The C-level command "testmenubarheight" deals with this issue but it may
    # not be available on each platform. Therefore, provide a fallback here.
    if {[llength [info commands testmenubarheight]] == 0} {
	if {[tk windowingsystem] ne "aqua"} {
	    # Windows may overlap the menubar
	    proc testmenubarheight {} {
		return 0
	    }
	} else {
	    # Windows may not overlap the menubar
	    proc testmenubarheight {} {
		return 30 ;  # arbitrary value known to be larger than the menubar height
	    }
	}
    }

    # testutils --
    #
    #    Takes care of exporting/importing/forgetting utility procs and any
    #    associated variables from a specific test domain (functional area).
    #
    #    More information is available in the file "testutils.GUIDE"
    #
    # Arguments:
    #    subCmd : "export", "import" or "forget"
    #    args   : a sequence of domains that need to be imported/forgotten,
    #             unused for "export"
    #
    proc testutils {subCmd args} {
	variable importedDomains
	variable importVars

	if {$subCmd ni [list export import forget]} {
	    return -code error "invalid subCmd \"$subCmd\". Usage: [lindex [info level 0] 0] export|import|forget ?domain domain ...?"
	}

	set argc [llength $args]
	if {$subCmd eq "export"} {
	    if {$argc != 0} {
		return -code error "invalid #args. Usage: [lindex [info level 0] 0] export"
	    }

	    # export all procs from the invoking domain namespace except "init"
	    uplevel 1 {
		if {[info procs init] eq "init"} {
		    set exports [info procs]
		    namespace export {*}[lremove $exports [lsearch $exports "init"]]
		    unset exports
		} else {
		    namespace export *
		}
	    }
	    return
	}
	if {$argc < 1} {
	    return -code error "invalid #args. Usage: [lindex [info level 0] 0] import|forget domain ?domain ...?"
	}

	# determine the requesting namespace
	set ns [uplevel 1 {namespace current}]

	# import/forget domains
	foreach domain $args {
	    if {! [namespace exists ::tk::test::$domain]} {
		return -code error "testutils domain \"$domain\" doesn't exist"
	    }

	    switch -- $subCmd {
		import {
		    if {[info exists importedDomains($ns)] && ($domain in $importedDomains($ns))} {
			return -code error "testutils domain \"$domain\" was already imported"
		    } else {

			# import procs
			if {[catch {
			    uplevel 1 [list namespace import ::tk::test::${domain}::*]
			} errMsg]} {
			    # revert import of procs already done
			    uplevel 1 [list namespace forget ::tk::test::${domain}::*]
			    return -code error "import from testutils domain \"$domain\" failed: $errMsg"
			}

			# import associated namespace variables declared in the init proc
			if {"init" in [namespace inscope ::tk::test::$domain {info procs init}]} {
			    if {[info exists importVars($ns,$domain)]} {
				#
				# Note [A1]:
				# If test files inadvertently leave behind a variable with the same name
				# as an upvar'ed namespace variable, its last value will serve as a new
				# initial value in case that the init proc declares that variable without
				# a value. Also, the result of "info exists varName" would be different
				# between test files.
				#
				# The next unset prevents such artefacts. See also note [A2] below.
				#
				uplevel 1 [list unset -nocomplain {*}$importVars($ns,$domain)]
			    }
			    ::tk::test::${domain}::init
			    if {($ns ne "::") || (! [info exists importVars($ns,$domain)])} {
				#
				# Importing associated namespace variables into the global namespace where
				# tests are normally executing, needs to be done only once because an upvar
				# link cannot be removed from a namespace. For other requesting namespaces
				# we need to reckon with deletion and re-creation of the namespace in the
				# meantime.
				#
				if {[info exists importVars($ns,$domain)]} {
				    set associatedVars $importVars($ns,$domain)
				} else {
				    set associatedVars [namespace inscope ::tk::test::$domain {info vars}]
				}
				foreach varName $associatedVars {
				    if {[catch {
					uplevel 1 [list upvar #0 ::tk::test::${domain}::$varName $varName]
				    } errMsg]} {
					# revert imported procs and partial variable import
					uplevel 1 [list unset -nocomplain {*}$associatedVars]
					uplevel 1 [list namespace forget ::tk::test::${domain}::*]
					return -code error "import from testutils domain \"$domain\" failed: $errMsg"
				    }
				}
				set importVars($ns,$domain) $associatedVars
			    }
			}

			# register domain as imported
			lappend importedDomains($ns) $domain
		    }
		}
		forget {
		    if {(! [info exists importedDomains($ns)]) || ($domain ni $importedDomains($ns))} {
			return -code error "testutils domain \"$domain\" was not imported"
		    }

		    # remove imported utility procs from the namespace where tests are executing
		    uplevel 1 [list namespace forget ::tk::test::${domain}::*]

		    #
		    # Some namespace variables are meant to persist across test files
		    # in the entire Tk test suite (notably the variable ImageNames,
		    # domain "image"). These variables are also not meant to be accessed
		    # from, and imported into the namespace where tests are executing,
		    # and they should not be cleaned up here.
		    #

		    if {[info exists importVars($ns,$domain)]} {
			#
			# Remove imported namespace variables.
			#
			# Note [A2]:
			# The upvar link in the namespace where tests are executing cannot be removed.
			# Without specific attention, this can cause surprising behaviour upon
			# re-initialization. See also note [A1] above.
			#
			uplevel 1 [list unset -nocomplain {*}$importVars($ns,$domain)]
		    }
		    set importedDomains($ns) [lremove $importedDomains($ns) [lsearch $importedDomains($ns) $domain]]
		}
	    }
	}
    }

    testutils export
}

# Import generic utility procs into the global namespace (in which tests are
# normally executing) as a standard policy.
::tk::test::generic::testutils import generic

namespace eval ::tk::test::button {
    proc bogusTrace args {
	error "trace aborted"
    }
    testutils export
}

namespace eval ::tk::test::child {

    # childTkInterp --
    #
    #	Create a new Tk application in a child interpreter, with
    #	a given name and class.
    #
    proc childTkInterp {name args} {
	set index [lsearch $args "-safe"]
	if {$index >= 0} {
	    set safe 1
	    set options [lremove $args $index]
	} else {
	    set safe 0
	    set options $args
	}
	if {[llength $options] ni {0 2}} {
	    return -code error "invalid #args"
	}

	set cmdArgs [list -name $name]
	foreach {key value} $options {
	    if {$key ne "-class"} {
		return -code error "invalid option \"$key\""
	    }
	    lappend cmdArgs $key $value
	}

	if {$safe} {
	    interp create -safe $name
	} else {
	    interp create $name
	}

	$name eval [list set argv $cmdArgs]
	catch {eval [loadTkCommand] $name}
    }

    # childTkProcess --
    #
    #	Create a new Tk application in a child process, and enable it to
    #	evaluate scripts on our behalf.
    #
    #	Suggestion: replace with child interp or thread ?
    #
    proc childTkProcess {subcmd args} {
	variable fd
	switch -- $subcmd {
	    create {
		if {[info exists fd] && [string length $fd]} {
		    childTkProcess exit
		}
		set fd [open "|[list [::tcltest::interpreter] \
			-geometry +0+0 -name tktest] $args" r+]
		puts $fd "puts foo; flush stdout"
		flush $fd
		if {[gets $fd data] < 0} {
		    error "unexpected EOF from \"[::tcltest::interpreter]\""
		}
		if {$data ne "foo"} {
		    error "unexpected output from\
			    background process: \"$data\""
		}
		puts $fd [loadTkCommand]
		flush $fd
		fileevent $fd readable [namespace code {childTkProcess read}]
	    }
	    eval {
		variable Data
		variable Done

		set script [lindex $args 0]
		set block 0
		if {[llength $args] == 2} {
		    set block [lindex $args 1]
		}

		if {$block} {
		    fileevent $fd readable {}
		}
		puts $fd "[list catch $script msg]; update; puts \$msg;\
			puts **DONE**; flush stdout"
		flush $fd
		set Data {}
		if {$block} {
		    while {![eof $fd]} {
			set line [gets $fd]
			if {$line eq "**DONE**"} {
			    break
			}
			append Data $line
		    }
		} else {
		    set Done 0
		    vwait [namespace which -variable Done]
		}
		return $Data
	    }
	    exit {
		# catch in case the child process has closed $fd
		catch {puts $fd exit}
		catch {close $fd}
		set fd ""
	    }
	    read {
		variable Data
		variable Done
		set x [gets $fd]
		if {[eof $fd]} {
		    fileevent $fd readable {}
		    set Done 1
		} elseif {$x eq "**DONE**"} {
		    set Done 1
		} else {
		    append Data $x
		}
	    }
	}
    }

    testutils export
}

namespace eval ::tk::test::colors {
    # colorsFree --
    #
    # Returns 1 if there appear to be free colormap entries in a window, 0
    # otherwise.
    #
    # Arguments:
    #	w                : name of window in which to check.
    #	red, green, blue : intensities to use in a trial color allocation
    #	                   to see if there are colormap entries free.
    #
    proc colorsFree {w {red 31} {green 245} {blue 192}} {
	lassign [winfo rgb $w [format "#%02x%02x%02x" $red $green $blue]] r g b
	expr {($r/256 == $red) && ($g/256 == $green) && ($b/256 == $blue)}
    }

    # eatColors --
    #
    # Creates a toplevel window and allocates enough colors in it to use up all
    # the slots in an 8-bit colormap.
    #
    # Arguments:
    #	w : name of toplevel window to create.
    #
    proc eatColors {w} {
	catch {destroy $w}
	toplevel $w
	wm geom $w +0+0
	canvas $w.c -width 400 -height 200 -bd 0
	pack $w.c
	for {set y 0} {$y < 8} {incr y} {
	    for {set x 0} {$x < 40} {incr x} {
		set color [format #%02x%02x%02x [expr {$x*6}] [expr {$y*30}] 0]
		$w.c create rectangle [expr {10*$x}] [expr {20*$y}] \
		    [expr {10*$x + 10}] [expr {20*$y + 20}] -outline {} \
		    -fill $color
	    }
	}
	update
    }

    testutils export
}

namespace eval ::tk::test::dialog {

    # init --
    #
    # This is a reserved proc that is part of the mechanism that the proc
    # testutils employs when making utility procs and associated namespace
    # variables available to test files.
    #
    # Test authors should define and initialize namespace variables here if
    # they need to be imported into the namespace in which tests are executing.
    # This proc must not be exported.
    #
    # For more information, see the documentation in the file "testutils.GUIDE"
    #
    proc init {} {
	variable dialogType [file rootname [file tail [info script]]]
	variable dialogIsNative [isNative $dialogType]
	variable testDialog
	variable testDialogFont
    }

    proc Click {button} {
	variable dialogType
	variable testDialog

	switch -- $dialogType {
	    "fontchooser" {
		if {$button ni "ok cancel apply"} {
		    return -code error "invalid button name \"$button\""
		}
		$testDialog.$button invoke
	    }
	    "winDialog" {
		switch -exact -- $button {
		    ok     { set button 1 }
		    cancel { set button 2 }
		}
		testwinevent $testDialog $button WM_LBUTTONDOWN 1 0x000a000b
		testwinevent $testDialog $button WM_LBUTTONUP 0 0x000a000b
	    }
	    default {
		return -code error "invalid dialog type \"$dialogType\""
	    }
	}
    }

    proc isNative {type} {
	switch -- $type {
	    "choosedir" {
		set cmd ::tk_chooseDirectory
	    }
	    "clrpick" {
		set cmd ::tk_chooseColor
	    }
	    "filebox" {
		set cmd ::tk_getOpenFile
	    }
	    "msgbox" {
		set cmd ::tk_messageBox
	    }
	    "dialog" -
	    "fontchooser" -
	    "winDialog" {
		return "N/A"
	    }
	    default {
		return -code error "invalid dialog type \"$type\""
	    }
	}
	return [expr {[info procs $cmd] eq ""}]
    }

    proc PressButton {btn} {
	event generate $btn <Enter>
	event generate $btn <Button-1> -x 5 -y 5
	event generate $btn <ButtonRelease-1> -x 5 -y 5
    }

    proc SendButtonPress {parent btn buttonType} {
	variable dialogType
	switch -- $dialogType {
	    "choosedir" {
		if {$parent eq "."} {
		    set w .__tk_choosedir
		} else {
		    set w $parent.__tk_choosedir
		}
		upvar ::tk::dialog::file::__tk_choosedir data
	    }
	    "clrpick" {
		set w .__tk__color
		upvar ::tk::dialog::color::[winfo name $w] data
	    }
	    "filebox" {
		if {$parent eq "."} {
		    set w .__tk_filedialog
		} else {
		    set w $parent.__tk_filedialog
		}
		upvar ::tk::dialog::file::__tk_filedialog data
	    }
	    "msgbox" {
		if {$parent eq "."} {
		    set w .__tk__messagebox
		} else {
		    set w $parent.__tk__messagebox
		}
	    }
	    default {
		return -code error "invalid dialog type \"$dialogType\""
	    }
	}

	if {$dialogType eq "msgbox"} {
	    set button $w.$btn
	} else {
	    set button $data($btn\Btn)
	}
	if {! [winfo ismapped $button]} {
	    update
	}

	if {$buttonType eq "mouse"} {
	    PressButton $button
	} else {
	    event generate $w <Enter>
	    focus $w
	    event generate $button <Enter>
	    event generate $w <Key> -keysym Return
	}
    }

    proc testDialog {stage {script ""}} {
	variable testDialogCmd
	variable testDialogResult
	variable testDialogFont
	variable iter_after
	variable testDialog; # On MS Windows, this variable is set at the C level
	                     # by SetTestDialog() in tkWinDialog.c

	switch -- $stage {
	    launch {
		set iter_after 0
		set testDialog {}
		if {$::tcl_platform(platform) eq "windows"} {
		    variable testDialogClass "#32770"
		}

		after 1 $script
	    }
	    onDisplay {
		set testDialogCmd $script
		set testDialogResult {}
		set testDialogFont {}

		if {$::tcl_platform(platform) eq "windows"} {
		    # Do not make the delay too short. The newer Vista dialogs take
		    # time to come up.
		    after 500 [list [namespace current]::testDialog onDisplay2]
		} else {
		    testDialog onDisplay2
		}
		vwait ::tk::test::dialog::testDialogResult
		return $testDialogResult
	    }
	    onDisplay2 {
		set doRepeat 0

		if {$::tcl_platform(platform) eq "windows"} {
		    # On Vista and later, using the new file dialogs we have to
		    # find the window using its title as testDialog will not be
		    # set at the C level.
		    variable testDialogClass
		    if {[catch {testfindwindow "" $testDialogClass} testDialog]} {
			set doRepeat 1
		    }
		} elseif {$testDialog eq ""} {
		    set doRepeat 1
		}

		if {$doRepeat} {
		    if {[incr iter_after] > 30} {
			set testDialogResult ">30 iterations waiting for testDialog"
			return
		    }
		    after 150 [list ::tk::test::dialog::testDialog onDisplay2]
		    return
		}
		set testDialogResult [uplevel #0 $testDialogCmd]
	    }
	    default {
		return -code error "invalid parameter \"$stage\""
	    }
	}
    }

    proc ToPressButton {parent btn} {
	variable dialogIsNative
	if {! $dialogIsNative} {
	    after 100 SendButtonPress $parent $btn mouse
	}
    }

    testutils export
}


namespace eval ::tk::test::entry {

    # init --
    #
    # This is a reserved proc that is part of the mechanism that the proc
    # testutils employs when making utility procs and associated namespace
    # variables available to test files.
    #
    # Test authors should define and initialize namespace variables here if
    # they need to be imported into the namespace in which tests are executing.
    # This proc must not be exported.
    #
    # For more information, see the documentation in the file "testutils.GUIDE"
    #
    proc init {} {
	variable textVar
	variable validationData
    }

    # Handler for variable trace on namespace variable textVar
    proc override args {
	variable textVar 12345
    }

    # Procedures used by widget validation tests
    proc validateCommand1 {W d i P s S v V} {
	variable validationData [list $W $d $i $P $s $S $v $V]
	return 1
    }
    proc validateCommand2 {W d i P s S v V} {
	variable validationData [list $W $d $i $P $s $S $v $V]
	variable textVar mydata
	return 1
    }
    proc validateCommand3 {W d i P s S v V} {
	variable validationData [list $W $d $i $P $s $S $v $V]
	return 0
    }
    proc validateCommand4 {W d i P s S v V} {
	variable validationData [list $W $d $i $P $s $S $v $V]
	.e delete 0 end;
	.e insert end dovaldata
	return 0
    }

    testutils export
}

namespace eval ::tk::test::geometry {
    proc getsize {w} {
	update
	return "[winfo reqwidth $w] [winfo reqheight $w]"
    }

    testutils export
}

namespace eval ::tk::test::image {

    proc imageCleanup {} {
	variable ImageNames
	foreach img [image names] {
	    if {$img ni $ImageNames} {image delete $img}
	}
    }

    proc imageFinish {} {
	variable ImageNames
	set imgs [lsearch -all -inline -glob -not [lsort [image names]] ::tk::icons::indicator*]
	if {$imgs ne $ImageNames} {
	    return -code error "images remaining: [image names] != $ImageNames"
	}
	imageCleanup
    }

    proc imageInit {} {
	variable ImageNames
	if {![info exists ImageNames]} {
	    set ImageNames [lsearch -all -inline -glob -not [lsort [image names]] ::tk::icons::indicator*]
	}
	imageCleanup
	if {[lsort [image names]] ne $ImageNames} {
	    return -code error "IMAGE NAMES mismatch: [image names] != $ImageNames"
	}
    }

    proc imageNames {} {
	variable ImageNames
	set r {}
	foreach img [image names] {
	    if {$img ni $ImageNames} {lappend r $img}
	}
	return $r
    }

    testutils export
}

namespace eval ::tk::test::scroll {

    # init --
    #
    # This is a reserved proc that is part of the mechanism that the proc
    # testutils employs when making utility procs and associated namespace
    # variables available to test files.
    #
    # Test authors should define and initialize namespace variables here if
    # they need to be imported into the namespace in which tests are executing.
    # This proc must not be exported.
    #
    # For more information, see the documentation in the file "testutils.GUIDE"
    #
    proc init {} {
	variable scrollInfo {}
    }

    # Used as the scrolling command for widgets, set with "-[xy]scrollcommand".
    # It saves the scrolling information in a namespace variable "scrollInfo".
    proc setScrollInfo {args} {
	variable scrollInfo $args
    }

    testutils export
}

namespace eval ::tk::test::select {

    # init --
    #
    # This is a reserved proc that is part of the mechanism that the proc
    # testutils employs when making utility procs and associated namespace
    # variables available to test files.
    #
    # Test authors should define and initialize namespace variables here if
    # they need to be imported into the namespace in which tests are executing.
    # This proc must not be exported.
    #
    # For more information, see the documentation in the file "testutils.GUIDE"
    #
    proc init {} {
	variable selValue {} selInfo {}
	variable abortCount
	variable pass
    }

    proc badHandler {path type offset count} {
	variable selInfo
	variable selValue
	selection handle -type $type $path {}
	lappend selInfo $path $type $offset $count
	set numBytes [expr {[string length $selValue] - $offset}]
	if {$numBytes <= 0} {
	    return ""
	}
	string range $selValue $offset [expr {$numBytes+$offset}]
    }

    proc badHandler2 {path type offset count} {
	variable abortCount
	variable selInfo
	variable selValue
	incr abortCount -1
	if {$abortCount == 0} {
	    selection handle -type $type $path {}
	}
	lappend selInfo $path $type $offset $count
	set numBytes [expr {[string length $selValue] - $offset}]
	if {$numBytes <= 0} {
	    return ""
	}
	string range $selValue $offset [expr {$numBytes+$offset}]
    }

    proc errHandler args {
	error "selection handler aborted"
    }

    proc errIncrHandler {type offset count} {
	variable selInfo
	variable selValue
	variable pass
	if {$offset == 4000} {
	    if {$pass == 0} {
		# Just sizing the selection;  don't do anything here.
		set pass 1
	    } else {
		# Fetching the selection;  wait long enough to cause a timeout.
		after 6000
	    }
	}
	lappend selInfo $type $offset $count
	set numBytes [expr {[string length $selValue] - $offset}]
	if {$numBytes <= 0} {
	    return ""
	}
	string range $selValue $offset [expr $numBytes+$offset]
    }

    proc handler {type offset count} {
	variable selInfo
	variable selValue
	lappend selInfo $type $offset $count
	set numBytes [expr {[string length $selValue] - $offset}]
	if {$numBytes <= 0} {
	    return ""
	}
	string range $selValue $offset [expr $numBytes+$offset]
    }

    proc reallyBadHandler {path type offset count} {
	variable selInfo
	variable selValue
	variable pass
	if {$offset == 4000} {
	    if {$pass == 0} {
		set pass 1
	    } else {
		selection handle -type $type $path {}
	    }
	}
	lappend selInfo $path $type $offset $count
	set numBytes [expr {[string length $selValue] - $offset}]
	if {$numBytes <= 0} {
	    return ""
	}
	string range $selValue $offset [expr {$numBytes+$offset}]
    }

    proc selectionSetup {{path .f1} {display {}}} {
	catch {destroy $path}
	if {$display eq ""} {
	    frame $path
	} else {
	    toplevel $path -screen $display
	    wm geom $path +0+0
	}
	selection own $path
    }

    testutils export
}

namespace eval ::tk::test::text {

    # init --
    #
    # This is a reserved proc that is part of the mechanism that the proc
    # testutils employs when making utility procs and associated namespace
    # variables available to test files.
    #
    # Test authors should define and initialize namespace variables here if
    # they need to be imported into the namespace in which tests are executing.
    # This proc must not be exported.
    #
    # For more information, see the documentation in the file "testutils.GUIDE"
    #
    proc init {} {
	variable fixedFont {Courier -12}
	variable fixedWidth [font measure $fixedFont m]
	variable fixedHeight [font metrics $fixedFont -linespace]
	variable fixedAscent [font metrics $fixedFont -ascent]
    }

    # full border size of the text widget, i.e. first x or y coordinate inside the text widget
    # warning:  -padx  is supposed to be the same as  -pady  (same border size horizontally and
    # vertically around the widget)
    proc bo {{w .t}} {
	return [expr {[$w cget -borderwidth] + [$w cget -highlightthickness] + [$w cget -padx]}]
    }

    # x-coordinate of the first pixel of $n-th char (count starts at zero), left justified
    proc xchar {n {w .t}} {
	return [expr {[bo $w] + [xw $n]}]
    }

    # x-width of $n chars, fixed width font
    proc xw {n} {
	variable fixedWidth
	return [expr {$n * $fixedWidth}]
    }

    # y-coordinate of the first pixel of $l-th display line (count starts at 1)
    proc yline {l {w .t}} {
	variable fixedHeight
	return [expr {[bo $w] + ($l - 1) * $fixedHeight}]
    }

    testutils export
}

namespace eval ::tk::test::timing {

    # init --
    #
    # This is a reserved proc that is part of the mechanism that the proc
    # testutils employs when making utility procs and associated namespace
    # variables available to test files.
    #
    # Test authors should define and initialize namespace variables here if
    # they need to be imported into the namespace in which tests are executing.
    # This proc must not be exported.
    #
    # For more information, see the documentation in the file "testutils.GUIDE"
    #
    proc init {} {
	variable dt
	set dt(granularity) milliseconds
	set dt(t0) [clock milliseconds]
    }

    proc dt.get {} {
	variable dt
	set now [clock $dt(granularity)]
	set result [expr {$now - $dt(t0)}]
	set dt(t0) $now
	return $result
    }

    proc dt.reset {{granularity milliseconds}} {
	if {$granularity ni "microseconds milliseconds seconds"} {
	    return -code error "invalid parameter \"$granularity\", expected \"microseconds\", \"milliseconds\" or \"seconds\""
	}
	variable dt
	set dt(granularity) $granularity
	set dt(t0) [clock $dt(granularity)]
    }

    # progress.* --
    #
    #	This set of procs monitors progress and total duration of a procedure
    #	in a loop.
    #
    #	Derived from tests/ttk/ttk.test, see:
    #
    #	https://core.tcl-lang.org/tk/file?ci=f94f84b254b0c5ad&name=tests/ttk/ttk.test&ln=335-340
    #
    proc progress.init {{granularity milliseconds}} {
	dt.reset $granularity
    }

    proc progress.update {} {
	puts -nonewline stderr "." ; flush stderr
    }

    proc progress.end {} {
	puts stderr " [dt.get] $::tk::test::timing::dt(granularity)"
    }

    testutils export
}

# EOF
