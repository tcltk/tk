# main.tcl --
#
# This file holds initialization code that is common to each testfile. In mode
# "-singleproc 0" it is loaded into each interpreter by invoking the command
# "tcltest::loadTestedCommands". In mode "-singleproc 1" it is sourced once into
# the current interpreter by all.tcl, before evaluating any test file.
#
# It performs an initial Tk setup for the root window, imports commands from
# the tcltest namespace, and loads definitions of global utility procs and
# test constraints.
#
# See the file "license.terms" for information on usage and redistribution
# of this file, and for a DISCLAIMER OF ALL WARRANTIES.

# Error out if this file is loaded repeatedly into the same interpreter
if {[namespace exists ::tk::test]} {
    return -code error "repeated loading of file \"main.tcl\""
}

#
# SETUP FOR APPLICATION AND ROOT WINDOW
#
encoding system utf-8
if {[tcltest::configure -singleproc] == 0} {
    # Support test suite invocation by tclsh (as is the case with "-singleproc 1")
    package require tk
}
tk appname tktest
wm title . tktest
wm geometry . +0+0

#
# IMPORT TCLTEST COMMANDS
#
namespace import -force tcltest::cleanupTests tcltest::interpreter \
	tcltest::makeDirectory tcltest::makeFile tcltest::removeDirectory \
	tcltest::removeFile tcltest::test tcltest::testsDirectory

#
# SOURCE DEFINITIONS OF GLOBAL UTILITY PROCS AND CONSTRAINTS
#
set mainTestDir [tcltest::configure -testdir]
if {[file tail $mainTestDir] eq "ttk"} {
    set mainTestDir [file dirname $mainTestDir]
}
source [file join $mainTestDir testutils.tcl]
source [file join $mainTestDir constraints.tcl]
unset mainTestDir

#
# RESET WINDOWS
#
resetWindows

# EOF
