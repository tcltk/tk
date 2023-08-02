if {[namespace exists tk::test]} {
    deleteWindows
    wm geometry . {}
    raise .
    return
}

package require Tk
tk appname tktest
wm title . tktest
# If the main window isn't already mapped (e.g. because the tests are
# being run automatically) , specify a precise size for it so that the
# user won't have to position it manually.

if {![winfo ismapped .]} {
    wm geometry . +0+0
    update
}

package require tcltest 2.2

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
                set ImageNames [lsort [image names]]
            }
            imageCleanup
            if {[lsort [image names]] ne $ImageNames} {
                return -code error "IMAGE NAMES mismatch: [image names] != $ImageNames"
            }
        }
        proc imageFinish {} {
            variable ImageNames
            if {[lsort [image names]] ne $ImageNames} {
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
	#    idle time.
	#    - In Tk releases 8.6 and older, pointer warping is scheduled for
	#      execution at idle time
	#    - In release 8.7 and newer this happens synchronously if $w refers to the
	#      whole screen or if the -when option to [event generate] is "now".
	#    The namespace variable idle_pointer_warping records which of these is
	#    the case.
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
	variable idle_pointer_warping [expr {![package vsatisfies [package provide Tk] 8.7-]}]
	proc controlPointerWarpTiming {{duration 50}} {
		variable idle_pointer_warping
		if {$idle_pointer_warping} {
			update idletasks ;# see a. above
		}
		if {[tk windowingsystem] eq "win32"} {
			after $duration ;# see b. above
		}
	}
	namespace export controlPointerWarpTiming

    }
}

namespace import -force tk::test::*

namespace import -force tcltest::testConstraint
testConstraint notAqua [expr {[tk windowingsystem] ne "aqua"}]
testConstraint aqua [expr {[tk windowingsystem] eq "aqua"}]
testConstraint x11 [expr {[tk windowingsystem] eq "x11"}]
testConstraint nonwin [expr {[tk windowingsystem] ne "win32"}]
testConstraint aquaOrWin32 [expr {
    ([tk windowingsystem] eq "win32") || [testConstraint aqua]
}]
testConstraint userInteraction 0
testConstraint nonUnixUserInteraction [expr {
    [testConstraint userInteraction] ||
    ([testConstraint unix] && [testConstraint notAqua])
}]
testConstraint haveDISPLAY [expr {[info exists env(DISPLAY)] && [testConstraint x11]}]
testConstraint altDisplay  [info exists env(TK_ALT_DISPLAY)]
testConstraint noExceed [expr {
    ![testConstraint unix] || [catch {font actual "\{xyz"}]
}]
# constraint for running a test on all windowing system except aqua
# where the test fails due to a known bug
testConstraint aquaKnownBug [expr {[testConstraint notAqua] || [testConstraint knownBug]}]

# constraints for testing facilities defined in the tktest executable...
testConstraint testImageType [expr {"test" in [image types]}]
testConstraint testOldImageType [expr {"oldtest" in [image types]}]
testConstraint testbitmap    [llength [info commands testbitmap]]
testConstraint testborder    [llength [info commands testborder]]
testConstraint testcbind     [llength [info commands testcbind]]
testConstraint testclipboard [llength [info commands testclipboard]]
testConstraint testcolor     [llength [info commands testcolor]]
testConstraint testcursor    [llength [info commands testcursor]]
testConstraint testembed     [llength [info commands testembed]]
testConstraint testfont      [llength [info commands testfont]]
testConstraint testmakeexist [llength [info commands testmakeexist]]
testConstraint testmenubar   [llength [info commands testmenubar]]
testConstraint testmetrics   [llength [info commands testmetrics]]
testConstraint testobjconfig [llength [info commands testobjconfig]]
testConstraint testsend      [llength [info commands testsend]]
testConstraint testtext      [llength [info commands testtext]]
testConstraint testwinevent  [llength [info commands testwinevent]]
testConstraint testwrapper   [llength [info commands testwrapper]]

# constraints about what sort of fonts are available
testConstraint fonts 1
destroy .e
entry .e -width 0 -font {Helvetica -12} -bd 1 -highlightthickness 1
.e insert end a.bcd
if {([winfo reqwidth .e] != 37) || ([winfo reqheight .e] != 20)} {
    testConstraint fonts 0
}
destroy .e
destroy .t
text .t -width 80 -height 20 -font {Times -14} -bd 1
pack .t
.t insert end "This is\na dot."
update
set x [list [.t bbox 1.3] [.t bbox 2.5]]
destroy .t
if {![string match {{22 3 6 15} {31 18 [34] 15}} $x]} {
    testConstraint fonts 0
}
# Although unexpected, some systems may have a very limited set of fonts available.
# The following constraints happen to evaluate to false at least on one system: the
# Github CI runner for Linux with --disable-xft, which has exactly ONE single font
# ([font families] returns a single element: "fixed"), for which [font actual]
# returns:
#    -family fixed -size 9 -weight normal -slant roman -underline 0
# and [font metrics] returns:
#    -ascent 11 -descent 2 -linespace 13 -fixed 1
# The following constraints are hence tailored to check exactly what is needed in the
# tests they constrain (that is: availability of any font having the given font
# attributes), so that these constrained tests will in fact run on all systems having
# reasonable font dotation.
testConstraint haveTimes12Font [expr {
    [font actual {times 12} -size] == 12
}]
testConstraint haveCourier37Font [expr {
    [font actual {-family courier -size 37} -size] == 37
}]
testConstraint haveTimes14BoldFont [expr {
    ([font actual {times 14 bold} -size] == 14) &&
    ([font actual {times 14 bold} -weight] eq "bold")
}]
testConstraint haveTimes12BoldItalicUnderlineOverstrikeFont [expr {
    ([font actual {times 12 bold italic overstrike underline} -weight] eq "bold") &&
    ([font actual {times 12 bold italic overstrike underline} -slant] eq "italic") &&
    ([font actual {times 12 bold italic overstrike underline} -underline] eq "1") &&
    ([font actual {times 12 bold italic overstrike underline} -overstrike] eq "1")
}]
set fixedFont {Courier 12}   ; # warning: must be consistent with the files using the constraint below!
set bigFont   {Helvetica 24} ; # ditto
testConstraint haveBigFontTwiceLargerThanTextFont [expr {
    [font actual $fixedFont -size] * 2 <= [font actual $bigFont -size]
}]
unset fixedFont bigFont

# constraints for the visuals available
testConstraint pseudocolor8 [expr {
    ([catch {
	toplevel .t -visual {pseudocolor 8} -colormap new
    }] == 0) && ([winfo depth .t] == 8)
}]
destroy .t
testConstraint haveTruecolor24 [expr {
    {truecolor 24} in [winfo visualsavailable .]
}]
testConstraint haveGrayscale8 [expr {
    {grayscale 8} in [winfo visualsavailable .]
}]
testConstraint defaultPseudocolor8 [expr {
    ([winfo visual .] eq "pseudocolor") && ([winfo depth .] == 8)
}]

# constraint based on whether our display is secure
setupbg
set app [dobg {tk appname}]
testConstraint secureserver 0
if {[llength [info commands send]]} {
    testConstraint secureserver 1
    if {[catch {send $app set a 0} msg] == 1} {
        if {[string match "X server insecure *" $msg]} {
            testConstraint secureserver 0
	}
    }
}
cleanupbg

eval tcltest::configure $argv
namespace import -force tcltest::test
namespace import -force tcltest::makeFile
namespace import -force tcltest::removeFile
namespace import -force tcltest::makeDirectory
namespace import -force tcltest::removeDirectory
namespace import -force tcltest::interpreter
namespace import -force tcltest::testsDirectory
namespace import -force tcltest::cleanupTests

deleteWindows
wm geometry . {}
raise .

