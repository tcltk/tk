# testutils.tcl --
#
# This file is sourced by each test file when invoking "tcltest::loadTestedCommands".
# It holds utility procs, each of which is used by several test files in the
# Tk test suite.
#
# See the file "license.terms" for information on usage and redistribution
# of this file, and for a DISCLAIMER OF ALL WARRANTIES.

#
# NAMESPACES AND FUNCTIONAL AREAS
#
# There are two types of utility proc: generic procs, and procs that belong to
# a specific functional area of Tk.
# - Generic utility procs reside in the namespace ::tk::test. These utility
#   procs are all imported by default for each test file ("namespace import"
#   command already issued in this file), and therefore they are readily
#   available to them.
# - Utility procs that belong to a specific functional area reside in a child
#   namespace of ::tk::test that bears the name of that functional area (for
#   example ::tk::test::dialog). These procs are imported on demand by test
#   files.
#

#
# DEFINITIONS OF GENERIC UTILITY PROCS
#

namespace eval tk {
    namespace eval test {

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

	# createStdAccessProc --
	#
	# Creates a standard proc for accessing a namespace variable, providing
	# get and set methods.
	#
	proc createStdAccessProc {varName} {
	    uplevel 1 [subst -nocommands {
		proc $varName {subcmd {value ""}} {
		    variable $varName
		    switch -- \$subcmd {
			get {
			    return \$$varName
			}
			set {
			    set $varName \$value
			}
			default {
			    return -code error "invalid subcmd \"\$subcmd\""
			}
		    }
		}
	    }]
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
	    set tklib {}
	    foreach pair [info loaded {}] {
		foreach {lib pfx} $pair break
		if {$pfx eq "Tk"} {
		    set tklib $lib
		    break
		}
	    }
	    return [list load $tklib Tk]
	}

	# On macOS windows are not allowed to overlap the menubar at the top of the
	# screen or the dock.  So tests which move a window and then check whether it
	# got moved to the requested location should use a y coordinate larger than the
	# height of the menubar (normally 23 pixels) and an x coordinate larger than the
	# width of the dock, if it happens to be on the left.
	# testmenubarheight deals with this issue but may not be available from the test
	# environment, therefore provide a fallback here
	if {[llength [info procs testmenubarheight]] == 0} {
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

	# Suspend script execution for a given amount of time, but continue
	# processing events.
	proc _pause {{msecs 1000}} {
	    variable _pause

	    if {! [info exists _pause(number)]} {
		set _pause(number) 0
	    }

	    set num [incr _pause(number)]
	    set _pause($num) 0

	    after $msecs "set _pause($num) 1"
	    vwait _pause($num)
	    unset _pause($num)
	}

	# testutils --
	#
	#    Takes care of importing/forgetting utility procs and any associated
	#    variables from a specific test domain (functional area). It hides
	#    details/peculiarities from the test author.
	#
	#    The "import" subcmd invokes any proc "init" defined in the domain-
	#    specific namespace. See also the explanation of this mehanism in
	#    this file at:
	#
	#  INIT PROCS, IMPORTING UTILITY PROCS AND ASSOCIATED NAMESPACE VARIABLES,
	#  AND AUTO-INITIALIZATION
	#
	# Arguments:
	#    subCmd : "import" or "forget"
	#    args   : a sequence of domains that need to be imported/forgotten,
	#             optionally preceded by the option -nocommands or -novars.
	#
	proc testutils {subCmd args} {
	    variable importedVars

	    set usage "[lindex [info level 0] 0] import|forget ?-nocommands|-novars? domain ?domain domain ...?"
	    set argc [llength $args]
	    if {$argc < 1} {
		return -code error $usage
	    }

	    set option [lindex $args 0]
	    if {$option ni [list -nocommands -novars]} {
		set option {}
	    }
	    if {($subCmd ni [list import forget]) || (($option ne "") && ($argc < 2))} {
		return -code error $usage
	    }
	    if {($subCmd eq "forget") && ($option ne "")} {
		return -code error "options \"-nocommands\" and \"-novars\" are not valid with subCmd \"forget\""
	    }

	    set domains [expr {$option eq ""?$args:[lrange $args 1 end]}]
	    foreach domain $domains {
		if {! [namespace exists ::tk::test::$domain]} {
		    return -code error "Tk test domain \"$domain\" doesn't exist"
		}
		switch -- $subCmd {
		    import {
			if {$domain ni [array names importedVars]} {
			    if {$option ne "-nocommands"} {
				uplevel 1 [list namespace import -force ::tk::test::${domain}::*]
				set importedVars($domain) [list]
			    }
			    if {$option ne "-novars"} {
				if {[namespace inscope ::tk::test::$domain {info procs init}] eq "init"} {
				    ::tk::test::${domain}::init
				    foreach varName [namespace inscope ::tk::test::$domain {info vars}] {
					if {[catch {
					    uplevel 1 [list upvar #0 ::tk::test::${domain}::$varName $varName]
					} errMsg]} {
					    return -code error "failed to import variable $varName from utility namespace ::tk::test::$domain into the namespace in which tests are executing: $errMsg"
					}
					# auto-re-initialize an imported namespace variable if the test file unsets it
					trace add variable $varName unset ::tk::test::${domain}::init
					lappend importedVars($domain) $varName
				    }
				}
			    }
			} else {
			    if {[namespace inscope ::tk::test::$domain {info procs init}] eq "init"} {
				::tk::test::${domain}::init
			    }
			}
		    }
		    forget {
			if {! [info exists importedVars($domain)]} {
			    return -code error "domain \"$domain\" was not imported"
			}
			uplevel 1 [list namespace forget ::tk::test::${domain}::*]
			foreach varName $importedVars($domain) {
			    trace remove variable $varName unset ::tk::test::${domain}::init
			}
			uplevel 1 [list unset -nocomplain {*}$importedVars($domain)]
			unset importedVars($domain)
		    }
		}
	    }
	}

	namespace export *
    }
}

# import generic utility procs into test files
namespace import -force tk::test::*

#
# DEFINITIONS OF UTILITY PROCS PER FUNCTIONAL AREA
#

#
#  INIT PROCS, IMPORTING UTILITY PROCS AND ASSOCIATED NAMESPACE VARIABLES,
#  AND AUTO-INITIALIZATION
#
# Some utility procs from specific functional areas store state in a namespace
# variable that is also accessed from the namespace in which the tests are
# executed (the "executing namespace"). Some tests require such variables
# to be initialized.
#
# When such variables are imported into the executing namespace through
# an "upvar" command, and the test file subsequently unsets these variables
# as part of a cleanup operation, this results in the deletion of the target
# variable inside the specific domain namespace. This, in turn, poses a problem
# for the next test file, which presumes that the variable is initialized.
#
# The proc "testutils" deals with this upvar issue as follows:
#
# - if a namespace for a specific functional area holds a proc "init", the
#   command "testutils import xxx" will invoke it, thus initializing namespace
#   variables. And it subsequently imports them into the executing namespace
#   using "upvar" (import with auto-initialization).
# - upon test file cleanup, "testutils forget xxx" will remove the imported
#   utility procs with the associated namespace variables, and unset the
#   upvar'ed variable in both the source and target namespace, including their
#   link. The link and the initialization will be restored for the next
#   test file when it invokes "testutils import xxx".
#
# Test authors who create a new utility proc that uses a namespace variable
# that is also accessed by a test file, need to add the initialization
# statements to the init proc. Just placing them inside the "namespace eval"
# scope for the specific domain (outside the init proc) isn't enough because
# that foregoes the importing of the namespace variables as well as their
# automatic initialization.
#

namespace eval ::tk::test::button {
    proc bogusTrace args {
	error "trace aborted"
    }
    namespace export *
}

namespace eval ::tk::test::child {

    # childTkInterp --
    #
    # 	Create a new Tk application in a child interpreter, with
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

	variable loadTkCmd
	if {! [info exists loadTkCmd]} {
	    foreach pkg [info loaded] {
		if {[lindex $pkg 1] eq "Tk"} {
		    set loadTkCmd "load $pkg"
		    break
		}
	    }
	}
	if {$safe} {
	    interp create -safe $name
	} else {
	    interp create $name
	}

	$name eval [list set argv $cmdArgs]
	catch {eval $loadTkCmd $name}
    }

    # childTkProcess --
    #
    # 	Create a new Tk application in a child process, and enable it to
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
		puts $fd [::tk::test::loadTkCommand]
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

    namespace export *
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

    namespace export *
}

namespace eval ::tk::test::dialog {

    # init --
    #
    # This is a reserved proc that is part of the mechanism that proc testutils
    # employs when importing utility procs and associated namespace variables
    # into the namespace in which a test file is executed.
    # See also the explanation in this file at:
    #
    #  INIT PROCS, IMPORTING UTILITY PROCS AND ASSOCIATED NAMESPACE VARIABLES,
    #  AND AUTO-INITIALIZATION
    #
    # Test authors should define namespace variables here if they need to be
    # imported into a test file namespace. This proc must not be exported.
    #
    proc init {args} {
	variable dialogType none
	variable testDialog
	variable testDialogFont
    }

    proc Click {button} {
	variable testDialog
	if {$button ni "ok cancel apply"} {
	    return -code error "invalid button name \"$button\""
	}
	$testDialog.$button invoke
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
	variable dialogType
	if {($dialogType eq "choosedir") || ! $::isNative} {
	    after 100 SendButtonPress $parent $btn mouse
	}
    }

    namespace export Click PressButton SendButtonPress testDialog \
	    ToPressButton
}


namespace eval ::tk::test::entry {

    # init --
    #
    # This is a reserved proc that is part of the mechanism that proc testutils
    # employs when importing utility procs and associated namespace variables
    # into the namespace in which a test file is executed.
    # See also the explanation in this file at:
    #
    #  INIT PROCS, IMPORTING UTILITY PROCS AND ASSOCIATED NAMESPACE VARIABLES,
    #  AND AUTO-INITIALIZATION
    #
    # Test authors should define namespace variables here if they need to be
    # imported into a test file namespace. This proc must not be exported.
    #
    proc init {args} {
	variable validationData
    }

    # Handler for variable trace on ::x
    proc override args {
	global x
	set x 12345
    }

    # Procedures used by widget validation tests
    proc validateCommand1 {W d i P s S v V} {
	variable validationData [list $W $d $i $P $s $S $v $V]
	return 1
    }
    proc validateCommand2 {W d i P s S v V} {
	variable validationData [list $W $d $i $P $s $S $v $V]
	set ::e mydata
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

    namespace export override validateCommand1 validateCommand2 validateCommand3 \
	    validateCommand4 unsetValidationData
}

namespace eval ::tk::test::geometry {
    proc getsize {w} {
	update
	return "[winfo reqwidth $w] [winfo reqheight $w]"
    }

    namespace export *
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

    namespace export *
}

namespace eval ::tk::test::scroll {

    # init --
    #
    # This is a reserved proc that is part of the mechanism that proc testutils
    # employs when importing utility procs and associated namespace variables
    # into the namespace in which a test file is executed.
    # See also the explanation in this file at:
    #
    #  INIT PROCS, IMPORTING UTILITY PROCS AND ASSOCIATED NAMESPACE VARIABLES,
    #  AND AUTO-INITIALIZATION
    #
    # Test authors should define namespace variables here if they need to be
    # imported into a test file namespace. This proc must not be exported.
    #
    proc init {args} {
	variable scrollInfo {}
    }

    # Used as the scrolling command for widgets, set with "-[xy]scrollcommand".
    # It saves the scrolling information in a namespace variable "scrollInfo".
    proc setScrollInfo {args} {
	variable scrollInfo $args
    }

    namespace export *
}

namespace eval ::tk::test::select {
    variable selValue {} selInfo {}

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
	set numBytes [expr {[string length [selValue get]] - $offset}]
	if {$numBytes <= 0} {
	    return ""
	}
	string range [selValue get] $offset [expr {$numBytes+$offset}]
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

    proc setup {{path .f1} {display {}}} {
	catch {destroy $path}
	if {$display eq ""} {
	    frame $path
	} else {
	    toplevel $path -screen $display
	    wm geom $path +0+0
	}
	selection own $path
    }

    # Create access procs for namespace variables
    foreach varName {abortCount pass selInfo selValue} {
	::tk::test::createStdAccessProc $varName
    }
    unset varName

    namespace export *
}

namespace eval ::tk::test::text {

    # init --
    #
    # This is a reserved proc that is part of the mechanism that proc testutils
    # employs when importing utility procs and associated namespace variables
    # into the namespace in which a test file is executed.
    # See also the explanation in this file at:
    #
    #  INIT PROCS, IMPORTING UTILITY PROCS AND ASSOCIATED NAMESPACE VARIABLES,
    #  AND AUTO-INITIALIZATION
    #
    # Test authors should define namespace variables here if they need to be
    # imported into a test file namespace. This proc must not be exported.
    #
    proc init {args} {
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

    namespace export bo xchar xw yline
}

# EOF
