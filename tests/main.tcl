# main.tcl --
#
# This file holds initialization code that is common to all test files.
# It performs an initial Tk setup for the root window, imports commands from
# the tcltest namespace, and loads definitions of global utility procs and
# test constraints.
#
# See the file "license.terms" for information on usage and redistribution
# of this file, and for a DISCLAIMER OF ALL WARRANTIES.

if {[namespace exists ::tk::test]} {
    # This file has already been sourced by a previous test file in mode -singleproc 1
    return
}

#
# SETUP FOR APPLICATION AND ROOT WINDOW
#
encoding system utf-8
if {[tcltest::configure -singleproc] == 0} {
    # Support test suite invocation by tclsh (as is the case with "-singleproc 1")
    package require tk

    #
    # Process non-tcltest options specific for the Tk test suite. These have
    # already been separated out by all.tcl, and stored in env(TK_TESTFLAGS).
    #
    if {[info exists env(TK_TESTFLAGS)]} {
	array set tktestConf $env(TK_TESTFLAGS)
    } else {
	array set tktestConf {}
    }
}
tk appname tktest
wm title . tktest

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

# EOF
