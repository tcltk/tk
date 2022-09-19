# all.tcl --
#
# This file contains a top-level script to run all of the Tcl
# tests.  Execute it by invoking "make test"
#

# restart using tclsh \
exec tclsh "$0" "$@"

package require Tk ;# This is for a Tk Widget; fail early if no Tk!
package require tcltest 2

tcltest::configure {*}$argv
tcltest::configure -testdir [file normalize [file dirname [info script]]]
tcltest::configure -singleproc 1
tcltest::runAllTests

