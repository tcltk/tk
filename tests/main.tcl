# main.tcl --
#
# This file is loaded by each test file when invoking "tcltest::loadTestedCommands".
# It performs an initial Tk setup for the root window, imports commands from
# the tcltest namespace, and loads definitions of global utility procs and
# test constraints.
#
# See the file "license.terms" for information on usage and redistribution
# of this file, and for a DISCLAIMER OF ALL WARRANTIES.

#
# SETUP FOR APPLICATION AND ROOT WINDOW
#
encoding system utf-8
if {[namespace exists tk::test]} {
    # reset windows
    deleteWindows
    wm geometry . {}
    raise .
    return
}

tk appname tktest
wm title . tktest
# If the main window isn't already mapped (e.g. because the tests are
# being run automatically) , specify a precise size for it so that the
# user won't have to position it manually.

if {![winfo ismapped .]} {
    wm geometry . +0+0
    update
}

#
# IMPORT TCLTEST COMMANDS
#
namespace import -force tcltest::cleanupTests tcltest::interpreter \
	tcltest::makeDirectory tcltest::makeFile tcltest::removeDirectory \
	tcltest::removeFile tcltest::test tcltest::testsDirectory

#
# SOURCE DEFINITIONS OF GLOBAL UTILITY PROCS AND CONSTRAINTS
#
# Note: tcltest uses [uplevel] to evaluate this script. Therefore, [info script]
#       cannot be used to determine the main Tk test directory, and we use
#       [tcltest::configure -loadfile] instead.
#
set mainTestDir [file dirname [tcltest::configure -loadfile]]
source [file join $mainTestDir testutils.tcl]
source [file join $mainTestDir constraints.tcl]
unset mainTestDir

#
# RESET WINDOWS
#
deleteWindows
wm geometry . {}
raise .

# EOF
