# all.tcl --
#
# This file contains a top-level script to run all of the ttk
# tests. Execute it by invoking "source all.tcl" when running tktest
# in this directory.
#
# Copyright © 2007 the Tk developers.
#
# See the file "license.terms" for information on usage and redistribution
# of this file, and for a DISCLAIMER OF ALL WARRANTIES.

#
# REQUIREMENTS
#
package require tk ;# This is the Tk test suite; fail early if no Tk!
package require tcltest 2.2

#
# CONFIGURATION FOR TCLTEST AND THE TK TEST SUITE
#

# Set defaults for the Tk test suite
tcltest::configure -singleproc 1

# Handle command line parameters
if {[llength $argv] & 1} {
    puts stderr "error: the number of command line parameters must be even (name - value pairs)."
    exit 1
}

# tcltest options that are not configurable for the Tk test suite
set ignoredOptions [list -testdir]

#
# Options that are specific for the Tk test suite:
#
# -eyefriendly	: if set to one, test execution halts at appropriate places to
#		  allow the human eye to follow the changes on the screen.
#
array set tktestConf {
    -eyefriendly 0
}

set index 0
array set indices {
    ignored {}
    tkTestSuite {}
}
foreach {key value} $argv {
    if {$key in $ignoredOptions} {
	lappend indices(ignored) $index
	puts stderr "warning: the Tk test suite ignores the option \"$key\" on the command line."
    } elseif {$key in [array names tktestConf]} {
	set tktestConf($key) $value
	lappend indices(tkTestSuite) $index
    }
    incr index 2
}
set tcltestOptions $argv
foreach index [lsort -decreasing [concat $indices(ignored) $indices(tkTestSuite)]] {
    set tcltestOptions [lreplace $tcltestOptions $index [expr {$index + 1}]]
}
tcltest::configure {*}$tcltestOptions
unset ignoredOptions index indices tcltestOptions

# Set tcltest options that are not user-configurable for the Tk test suite
tcltest::configure -testdir [file normalize [file dirname [info script]]]

# Determine test files to skip
set skipFiles [tcltest::configure -notfile]
if {"vista" ni [ttk::style theme names]} {
    lappend skipFiles vsapi.test
    tcltest::configure -notfile [lsort -unique $skipFiles]
}
unset skipFiles

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
