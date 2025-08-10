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
tcltest::configure {*}$argv

# Set tcltest options that are not user-configurable for the Tk test suite
tcltest::configure -testdir [file normalize [file dirname [info script]]]
if {[tcltest::configure -singleproc]} {
    #
    # All test files are evaluated in the current interpreter. We need to load
    # the file main.tcl only once.
    #
    source [file join [file dirname [tcltest::testsDirectory]] main.tcl]
} else {
    #
    # Each test file is evaluated in a separate process/interpreter. Each testfile
    # needs to load the file main.tcl into its interpreter.
    #
    tcltest::configure -loadfile \
	[file join [file dirname [tcltest::testsDirectory]] main.tcl]
}

#
# RUN ALL TESTS
#

# Note: the environment variable ERROR_ON_FAILURES is set by Github CI
if {[tcltest::runAllTests] && [info exists env(ERROR_ON_FAILURES)]} {
    exit 1
}
