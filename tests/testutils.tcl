# testutils.tcl --
#
# This file holds utility procs, each of which is used by several test files
# in the Tk test suite.
#
# The procs are defined per functional area of Tk (also called "domain"),
# similar to the names of test files:
# - generic utility procs that don't belong to a specific functional area go
#   into the namespace ::tk::test.
# - those that do belong to a specific functional area go into a child namespace
#   of ::tk::test that bears the name of that functional area.
#
# See the file "license.terms" for information on usage and redistribution
# of this file, and for a DISCLAIMER OF ALL WARRANTIES.

#
# DEFINITIONS OF GENERIC UTILITY PROCS
#
namespace eval tk {
    namespace eval test {

	namespace export loadTkCommand
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

	namespace eval bg {
	    # Manage a background process.
	    # Replace with child interp or thread?
	    namespace import ::tcltest::interpreter
	    namespace import ::tk::test::loadTkCommand
	    namespace export setup cleanup do

	    proc cleanup {} {
		variable fd
		# catch in case the background process has closed $fd
		catch {puts $fd exit}
		catch {close $fd}
		set fd ""
	    }
	    proc setup args {
		variable fd
		if {[info exists fd] && [string length $fd]} {
		    cleanup
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
	    proc do {cmd {block 0}} {
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
	}

	proc Export {internal as external} {
	    uplevel 1 [list namespace import $internal]
	    uplevel 1 [list rename [namespace tail $internal] $external]
	    uplevel 1 [list namespace export $external]
	}
	Export bg::setup as setupbg
	Export bg::cleanup as cleanupbg
	Export bg::do as dobg

	namespace export deleteWindows
	proc deleteWindows {} {
	    destroy {*}[winfo children .]
	    # This update is needed to avoid intermittent failures on macOS in unixEmbed.test
	    # with the (GitHub Actions) CI runner.
	    # Reason for the failures is unclear but could have to do with window ids being deleted
	    # after the destroy command returns. The detailed mechanism of such delayed deletions
	    # is not understood, but it appears that this update prevents the test failures.
	    update
	}

	namespace export fixfocus
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

	namespace export imageInit imageFinish imageCleanup imageNames
	variable ImageNames
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
	proc imageFinish {} {
	    variable ImageNames
	    set imgs [lsearch -all -inline -glob -not [lsort [image names]] ::tk::icons::indicator*]
	    if {$imgs ne $ImageNames} {
		return -code error "images remaining: [image names] != $ImageNames"
	    }
	    imageCleanup
	}
	proc imageCleanup {} {
	    variable ImageNames
	    foreach img [image names] {
		if {$img ni $ImageNames} {image delete $img}
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

	#
	#  CONTROL TIMING ASPECTS OF POINTER WARPING
	#
	# The proc [controlPointerWarpTiming] is intended to ensure that the (mouse)
	# pointer has actually been moved to its new position after a Tk test issued:
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
	namespace export controlPointerWarpTiming

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
    }
}

namespace import -force tk::test::*

#
# DEFINITIONS OF UTILITY PROCS PER FUNCTIONAL AREA
#
# Utility procs are defined and used per functional area of Tk as indicated by
# the names of test files. The namespace names below ::tk::test correspond to
# these functional areas.
#

namespace eval ::tk::test::scroll {

    # scrollInfo --
    #
    #	Used as the scrolling command for widgets, set with "-[xy]scrollcommand".
    #	It saves the scrolling information in, or retrieves it from a namespace
    #	variable "scrollInfo".
    #
    variable scrollInfo {}
    proc scrollInfo {mode args} {
	variable scrollInfo
	switch -- $mode {
	    get {
		return $scrollInfo
	    }
	    set {
		set scrollInfo $args
	    }
	}
    }

    namespace export *
}

namespace eval ::tk::test::select {

    proc errHandler args {
	error "selection handler aborted"
    }

    namespace export *
}

#
# TODO: RELOCATE UTILITY PROCS CATEGORY B. HERE
#       (As indicated by the spreadsheet file "relocate.ods")
#

# EOF
