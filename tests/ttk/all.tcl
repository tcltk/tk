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
set fixedOptions [list -testdir]
set newArgv $argv
foreach {key value} $argv {
    if {$key in $fixedOptions} {
	set newArgv [lreplace $newArgv $index [incr index]]
	puts stderr "warning: the Tk test suite ignores the option \"$key\" on the command line."
    } else {
	incr index 2
    }
}
tcltest::configure {*}$newArgv
unset fixedOptions newArgv

# Set tcltest options that are not user-configurable for the Tk test suite
tcltest::configure -testdir [file normalize [file dirname [info script]]]

#
# RUN ALL TESTS
#

# Note: the environment variable ERROR_ON_FAILURES is set by Github CI
if {[tcltest::runAllTests] && [info exists env(ERROR_ON_FAILURES)]} {
    exit 1
}
