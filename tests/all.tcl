# all.tcl --
#
# This file contains a top-level script to run all of the Tk
# tests.  Execute it by invoking "source all.tcl" when running tktest
# in this directory.
#
# Copyright © 1998-1999 Scriptics Corporation.
#
# See the file "license.terms" for information on usage and redistribution
# of this file, and for a DISCLAIMER OF ALL WARRANTIES.

package require tk ;# This is the Tk test suite; fail early if no Tk!
package require tcltest 2.2
tcltest::configure {*}$argv
tcltest::configure -testdir [file normalize [file dirname [info script]]]
tcltest::configure -loadfile \
    [file join [tcltest::testsDirectory] main.tcl]
tcltest::configure -singleproc 1
set ErrorOnFailures [info exists env(ERROR_ON_FAILURES)]
encoding system utf-8
if {[tcltest::runAllTests] && $ErrorOnFailures} {exit 1}
