# main.tcl --
#
# This file is loaded by each test file when invoking "tcltest::loadTestedCommands".
# It performs an initial Tk setup for the root window, and loads, in turn,
# definitions of global utility procs and test constraints.
#
# See the file "license.terms" for information on usage and redistribution
# of this file, and for a DISCLAIMER OF ALL WARRANTIES.

#
# SETUP FOR APPLICATION AND ROOT WINDOW
#
if {[namespace exists tk::test]} {
    # reset windows
    deleteWindows
    wm geometry . {}
    raise .
    return
}

package require tk
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
# LOAD AND CONFIGURE TEST HARNESS
#
package require tcltest 2.2
eval tcltest::configure $argv
namespace import -force tcltest::test
namespace import -force tcltest::makeFile
namespace import -force tcltest::removeFile
namespace import -force tcltest::makeDirectory
namespace import -force tcltest::removeDirectory
namespace import -force tcltest::interpreter
namespace import -force tcltest::testsDirectory
namespace import -force tcltest::cleanupTests

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
