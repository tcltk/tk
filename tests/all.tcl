# all.tcl --
#
# This file contains a top-level script to run all of the Tk
# tests.  Execute it by invoking "source all.tcl" when running tktest
# in this directory.
#
# Copyright (c) 1998-1999 by Scriptics Corporation.
#
# See the file "license.terms" for information on usage and redistribution
# of this file, and for a DISCLAIMER OF ALL WARRANTIES.

package require Tk ;# This is the Tk test suite; fail early if no Tk!
package require tcltest 2.2
# Without this hook macOS 10.15 (Catalina) segfaults due to passing a freed
# clientdata pointer to TkpDisplayButton.
proc tcltest::cleanupTestsHook {} {update idletasks}
tcltest::configure {*}$argv
tcltest::configure -testdir [file normalize [file dirname [info script]]]
tcltest::configure -loadfile \
	[file join [tcltest::testsDirectory] constraints.tcl]
tcltest::configure -singleproc 1
tcltest::runAllTests
