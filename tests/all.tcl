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
#
# RCS: @(#) $Id: all.tcl,v 1.9.2.2 2007/09/19 17:28:25 dgp Exp $

package require Tcl 8.5
package require tcltest 2.2
package require Tk ;# This is the Tk test suite; fail early if no Tk!
tcltest::configure {*}$argv
tcltest::configure -testdir [file normalize [file dirname [info script]]]
tcltest::configure -loadfile \
	[file join [tcltest::testsDirectory] constraints.tcl]
tcltest::configure -singleproc 1
tcltest::runAllTests
