# testutils.tcl --
#
# This file holds utility procs, each of which is used by several test files
# in the Tk test suite.
#
# See the file "license.terms" for information on usage and redistribution
# of this file, and for a DISCLAIMER OF ALL WARRANTIES.

#
# NAMESPACES AND FUNCTIONAL AREAS
#
# Utility procs are defined per functional area of Tk (also called "domain"),
# similar to the names of test files.
# - generic utility procs that don't belong to a specific functional area go
#   into the namespace ::tk::test.
# - those that do belong to a specific functional area go into a child namespace
#   of ::tk::test that bears the name of that functional area.
#

#
# DEFINITIONS OF GENERIC UTILITY PROCS
#
namespace eval tk {
    namespace eval test {

	proc assert {expr {message ""}} {
	    if {![uplevel 1 [list expr $expr]]} {
		error "PANIC: $message ($expr failed)"
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
	# menubarheight deals with this issue but may not be available from the test
	# environment, therefore provide a fallback here
	if {[llength [info procs menubarheight]] == 0} {
	    if {[tk windowingsystem] ne "aqua"} {
		# Windows may overlap the menubar
		proc menubarheight {} {
		    return 0
		}
	    } else {
		# Windows may not overlap the menubar
		proc menubarheight {} {
		    return 30 ;  # arbitrary value known to be larger than the menubar height
		}
	    }
	    namespace export menubarheight
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

	namespace export assert controlPointerWarpTiming createStdAccessProc deleteWindows fixfocus loadTkCommand _pause
    }
}

# Generic utilities are imported by default
namespace import -force tk::test::*

#
# DEFINITIONS OF UTILITY PROCS PER FUNCTIONAL AREA
#
# Utility procs are defined and used per functional area of Tk as indicated by
# the names of test files. The namespace names below ::tk::test correspond to
# these functional areas.
#

namespace eval ::tk::test::bg {
    # Manage a background process.
    # Replace with child interp or thread?
    namespace import ::tcltest::interpreter
    namespace import ::tk::test::loadTkCommand

    proc cleanupbg {} {
	variable fd
	# catch in case the background process has closed $fd
	catch {puts $fd exit}
	catch {close $fd}
	set fd ""
    }

    proc dobg {cmd {block 0}} {
	variable fd
	variable Data
	variable Done
	if {$block} {
	    fileevent $fd readable {}
	}
	puts $fd "[list catch $cmd msg]; update; puts \$msg;\
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

    proc Ready {} {
	variable fd
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

    proc setupbg args {
	variable fd
	if {[info exists fd] && [string length $fd]} {
	    cleanupbg
	}
	set fd [open "|[list [interpreter] \
		-geometry +0+0 -name tktest] $args" r+]
	puts $fd "puts foo; flush stdout"
	flush $fd
	if {[gets $fd data] < 0} {
	    error "unexpected EOF from \"[interpreter]\""
	}
	if {$data ne "foo"} {
	    error "unexpected output from\
		    background process: \"$data\""
	}
	puts $fd [loadTkCommand]
	flush $fd
	fileevent $fd readable [namespace code Ready]
    }

    namespace export cleanupbg dobg setupbg
}

namespace eval ::tk::test::button {
    proc bogusTrace args {
	error "trace aborted"
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
    # w -			Name of window in which to check.
    # red, green, blue -	Intensities to use in a trial color allocation
    #			to see if there are colormap entries free.
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
    # w -		Name of toplevel window to create.
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

    #
    # The following helper functions serve both windows and non-windows dialogs.
    # On windows (test file winDialog.test), they are used to send messages
    # to the win32 dialog, hence the weirdness.
    #

    proc afterbody {} {
	set doRepeat 0

	if {$::tcl_platform(platform) eq "windows"} {
	    # On Vista and later, using the new file dialogs we have to find
	    # the window using its title as tk_dialog will not be set at the C level
	    variable dialogclass
	    if {[catch {testfindwindow "" $dialogclass} ::tk_dialog]} {
		set doRepeat 1
	    }
	} elseif {$::tk_dialog eq ""} {
	    set doRepeat 1
	}

	if {$doRepeat} {
	    variable iter_after
	    if {[incr iter_after] > 30} {
		variable dialogresult ">30 iterations waiting for tk_dialog"
		return
	    }
	    after 150 ::tk::test::dialog::afterbody
	    return
	}

	variable dialogresult [uplevel #0 $::tk::test::dialog::dialogcommand]
    }

    # dialogTestFont --
    #
    # A global command "::testfont" (all lower case) is already defined by
    # tkTest.c for usage by the test file font.test. To distinguish our proc
    # from this global command, we use a prefix "dialog".
    #
    proc dialogTestFont {subcmd {font ""}} {
	variable testfont
	switch -- $subcmd {
	    get {
		return $testfont
	    }
	    set {
		set testfont $font
	    }
	    default {
		return -code error "invalid subcmd \"$subcmd\""
	    }
	}
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
		if {$parent == "."} {
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
		if {$parent == "."} {
		    set w .__tk_filedialog
		} else {
		    set w $parent.__tk_filedialog
		}
		upvar ::tk::dialog::file::__tk_filedialog data
	    }
	    "msgbox" {
		if {$parent == "."} {
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

	if {$buttonType == "mouse"} {
	    PressButton $button
	} else {
	    event generate $w <Enter>
	    focus $w
	    event generate $button <Enter>
	    event generate $w <Key> -keysym Return
	}
    }

    variable dialogType none
    proc setDialogType {type} {
	variable dialogType $type
    }

    proc start {script} {
	variable iter_after 0

	set ::tk_dialog {}
	if {$::tcl_platform(platform) eq "windows"} {
	    variable dialogclass "#32770"
	}

	after 1 $script
    }

    proc ToPressButton {parent btn} {
	variable dialogType
	if {($dialogType eq "choosedir") || ! $::isNative} {
	    after 100 SendButtonPress $parent $btn mouse
	}
    }

    proc then {cmd} {
	variable dialogcommand $cmd dialogresult {}
	variable testfont {}

	if {$::tcl_platform(platform) eq "windows"} {
	    # Do not make the delay too short. The newer Vista dialogs take
	    # time to come up. Even if the testforwindow returns true, the
	    # controls are not ready to accept messages
	    after 500 ::tk::test::dialog::afterbody
	} else {
	    afterbody
	}
	vwait ::tk::test::dialog::dialogresult
	return $dialogresult
    }

    namespace export dialogTestFont PressButton SendButtonPress setDialogType ToPressButton start then
}


namespace eval ::tk::test::entry {
    # For trace add variable
    proc override args {
	global x
	set x 12345
    }

    # Procedures used in widget VALIDATION tests
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
    proc validationData {subcmd {value ""}} {
	variable validationData
	switch -- $subcmd {
	    get {
		return $validationData
	    }
	    lappend {
		lappend validationData $value
	    }
	    set {
		set validationData $value
	    }
	    unset {
		unset -nocomplain validationData
	    }
	    default {
		return -code error "invalid subcmd \"$subcmd\""
	    }
	}
    }

    namespace export *
}

namespace eval ::tk::test::geometry {
    proc getsize {w} {
	update
	return "[winfo reqwidth $w] [winfo reqheight $w]"
    }

    namespace export *
}

namespace eval ::tk::test::image {
    variable ImageNames

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

    # scrollInfo --
    #
    #	Used as the scrolling command for widgets, set with "-[xy]scrollcommand".
    #	It saves the scrolling information in, or retrieves it from a namespace
    #	variable "scrollInfo".
    #
    variable scrollInfo {}
    proc scrollInfo {subcmd args} {
	variable scrollInfo
	switch -- $subcmd {
	    get {
		return $scrollInfo
	    }
	    set {
		set scrollInfo $args
	    }
	    default {
		return -code error "invalid subcmd \"$subcmd\""
	    }
	}
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
	if {$display == {}} {
	    frame $path
	} else {
	    toplevel $path -screen $display
	    wm geom $path +0+0
	}
	selection own $path
    }

    #
    # Create procs to be used for namespace variable access by test files
    #
    namespace import ::tk::test::createStdAccessProc
    foreach varName {abortCount pass selInfo selValue} {
	createStdAccessProc $varName
    }

    namespace export *
}

namespace eval ::tk::test::send {
    # Procedure to create a new application with a given name and class.
    proc newApp {name args} {

	set cmdArgs [list -name $name]

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

    namespace export *
}

namespace eval ::tk::test::text {
    variable fixedFont {Courier -12}
    variable fixedWidth [font measure $fixedFont m]
    variable fixedHeight [font metrics $fixedFont -linespace]
    variable fixedAscent [font metrics $fixedFont -ascent]

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

    namespace export *
}

# EOF
