package require Tcl 8.4
package require Tk 8.4
package require tcltest 2.1
namespace import -force tcltest::testConstraint
testConstraint userInteraction 0
testConstraint altDisplay [info exists env(TK_ALT_DISPLAY)]
testConstraint noExceed [expr {![testConstraint unix]
			|| [catch {font actual "\{xyz"}]}]
testConstraint testImageType [expr {[lsearch [image types] test] >= 0}]
testConstraint fonts 1
destroy .e
entry .e -width 0 -font {Helvetica -12} -bd 1
.e insert end a.bcd
if {([winfo reqwidth .e] != 37) || ([winfo reqheight .e] != 20)} {
    testConstraint fonts 0
}
destroy .e
text .t -width 80 -height 20 -font {Times -14} -bd 1
pack .t
.t insert end "This is\na dot."
update
set x [list [.t bbox 1.3] [.t bbox 2.5]]
destroy .t
if {![string match {{22 3 6 15} {31 18 [34] 15}} $x]} {
    testConstraint fonts 0
}

namespace eval ::tk {
    if {[namespace exists test]} {
	namespace delete test
    }
    namespace eval test {
	namespace eval bg {
	    # Manage a background process.  
	    # Replace with slave interp or thread?
	    namespace import ::tcltest::interpreter
	    namespace export setup cleanup do

	    proc cleanup {} {
		variable fd
		# catch in case the background process has closed $fd
		catch {puts $fd exit}
		catch {close $fd}
		set fd ""
	    }
	    proc setup args {
		variable fd
		if {[info exists fd] && [string length $fd]} {
		    cleanup
		}
		set fd [open "|[list [interpreter] \
			-geometry +0+0 -name tktest] $args" r+]
		puts $fd "puts foo; flush stdout"
		flush $fd
		if {[gets $fd data] < 0} {
		    error "unexpected EOF from \"[interpreter]\""
		}
		if {$data ne "foo"} {
		    error "unexpected output from\
			    background process: \"$data\""
		}
		fileevent $fd readable [namespace code Ready]
	    }
	    proc Ready {} {
		variable fd
		variable Data
		variable Done
		set x [gets $fd]
		if {[eof $fd]} {
		    fileevent $fd readable {}
		    set Done 1
		} elseif {$x eq "**DONE**"} {
		    set Done 1
		} else {
		    append Data $x
		}
	    }
	    proc do {cmd {block 0}} {
		variable fd
		variable Data
		variable Done
		if {$block} {
		    fileevent $fd readable {}
		}
		puts $fd "[list catch $cmd msg]; update; puts \$msg;\
			puts **DONE**; flush stdout"
		flush $fd
		set Data {}
		if {$block} {
		    while {![eof $fd]} {
			set line [gets $fd]
			if {$line eq "**DONE**"} {
			    break
			}
			append Data $line
		    }
		} else {
		    set Done 0
		    vwait [namespace which -variable Done]
		}
		return $Data
	    }
	}

	proc Export {internal as external} {
	    uplevel 1 [list namespace import $internal]
	    uplevel 1 [list rename [namespace tail $internal] $external]
	    uplevel 1 [list namespace export $external]
	}
	Export bg::setup as setupbg
	Export bg::cleanup as cleanupbg
	Export bg::do as dobg
    }
}

namespace import -force ::tk::test::*

