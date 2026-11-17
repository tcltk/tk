# all.tcl --
#
# This file contains a top-level script to run all of the ttk
# tests. Execute it by invoking "source all.tcl" when running tktest
# in this directory.
#
# Copyright © 2007 by the Tk developers.
#
# See the file "license.terms" for information on usage and redistribution
# of this file, and for a DISCLAIMER OF ALL WARRANTIES.

#
# REQUIREMENTS
#
package require Tk ;# This is the Tk test suite; fail early if no Tk!
package require tcltest 2.2
tcltest::configure {*}$argv
# Set tcltest options that are not user-configurable for the Tk test suite
tcltest::configure -testdir [file normalize [file dirname [info script]]]

tcltest::configure -loadfile \
    [file join [file dirname [tcltest::testsDirectory]] constraints.tcl]
tcltest::configure -singleproc 1
encoding system utf-8
#
# RUN ALL TESTS
#

# Note: the environment variable ERROR_ON_FAILURES is set by Github CI
# Under Tk, tcltest::cleanupTests ends the process with exit status 0 when
# runAllTests calls it after the last test file, so runAllTests would neither
# list the test files that exited with errors nor return whether anything
# failed. Prevent that exit with the "interactive" constraint: at that point
# all tests have run, so it no longer affects which tests run.
# Tcl ticket 7abc1460c0; not needed with a tcltest where it is fixed.
rename tcltest::cleanupTests tcltest::CleanupTestsAndExit
proc tcltest::cleanupTests {{calledFromAllFile 0}} {
    if {$calledFromAllFile} {
	testConstraint interactive 1
    }
    tailcall CleanupTestsAndExit $calledFromAllFile
}

set failed [tcltest::runAllTests]
exit [expr {$failed && [info exists env(ERROR_ON_FAILURES)]}]
