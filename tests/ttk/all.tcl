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
# TCLTEST CONFIGURATION
#

# Set defaults for the Tk test suite
tcltest::configure -singleproc 1

# Handle command line parameters
if {[llength $argv] & 1} {
    puts stderr "error: the number of command line parameters must be even (name - value pairs)."
    exit 1
}
set ignoredOptions [list -testdir]
set ignoredIndices [list ]
set index 0
foreach {key value} $argv {
    if {$key in $ignoredOptions} {
	lappend ignoredIndices $index
	puts stderr "warning: the Tk test suite ignores the option \"$key\" on the command line."
    }
    incr index 2
}
set tcltestOptions $argv
foreach index [lreverse $ignoredIndices] {
    set tcltestOptions [lreplace $tcltestOptions $index [expr {$index + 1}]]
}
tcltest::configure {*}$tcltestOptions
unset ignoredIndices ignoredOptions index tcltestOptions

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
if {[tcltest::runAllTests] && [info exists env(ERROR_ON_FAILURES)]} {
    exit 1
}
