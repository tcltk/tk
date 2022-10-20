# FILE: unixconsole.tcl
#
#       Provides a console window.
#
# Last modified on: $Date: 2005-10-15 06:00:15 $
# Last modified by: $Author: jcw $
#
# This file is evaluated to provide a console window interface to the
# root Tcl interpreter of an OOMMF application.  It calls on a script
# included with the Tk script library to do most of the work, making use
# of Tk interface details which are only semi-public.  For this reason,
# there is some risk that future versions of Tk will no longer support
# this script.  That is why this script has been isolated in a file of
# its own.
 
########################################################################
# Check Tcl/Tk support
########################################################################
package require Tcl 8.6-
package require Tk 8

namespace eval tk::unixconsole {
    variable conimpl [file join $tk_library console.tcl]
    if {![file readable $conimpl]} {
	return -code error "File not readable: $conimpl"
    }
    variable consoleInterp ""

    ########################################################################
    # Provide the support which the Tk library script console.tcl assumes
    ########################################################################

    proc conInterp {} {
	variable consoleInterp
	if {$consoleInterp eq ""} {
	    # Create an interpreter for the console window widget and load Tk
	    set consoleInterp [interp create]
	    $consoleInterp eval [list set tk_library $::tk_library]
	    $consoleInterp alias exit exit
	    $consoleInterp alias consoleinterp [namespace which consoleinterp]
	    $consoleInterp alias conDelete [namespace which conDelete]
	    load "" Tk $consoleInterp
	    # Bind the <Destroy> event of the application interpreter's main
	    # window to kill the console (via tk::ConsoleExit)
	    bind . <Destroy> [list +if {"%W" eq "."} [list catch \
	      [list $consoleInterp eval tk::ConsoleExit]]]
	    # Evaluate the Tk library console script in the console interpreter
	    variable conimpl
	    $consoleInterp eval [list source $conimpl]
	    $consoleInterp eval {
		bind Console <Destroy> +conDelete
		# addition by Schelte Bron ([sbron]):
		# Allow functional pasting with the middle mouse button
		bind Console <<PasteSelection>> {
		    if {$tk_strictMotif || ![info exists tk::Priv(mouseMoved)] \
		      || !$tk::Priv(mouseMoved)} {
			catch {
			    set clip [::tk::GetSelection %W PRIMARY]
			    set list [lassign [split $clip \n\r] x]
			    tk::ConsoleInsert %W $x
			    foreach x $list {
				%W mark set insert {end - 1c}
				tk::ConsoleInsert %W "\n"
				tk::ConsoleInvoke
				tk::ConsoleInsert %W $x
			    }
			}
		    }
		}
	    }
	    # Redirect stdout/stderr messages to the console
	    chan push stdout [list [namespace which tkConsoleOut] stdout]
	    chan push stderr [list [namespace which tkConsoleOut] stderr]
	    # No matter what Tk_Main says, insist this is an interactive shell
	    set ::tcl_interactive 1
	    title "[wm title .] Console"
	}
	return $consoleInterp
    }

    # A command 'console' in the application interpreter
    namespace ensemble create -command console \
      -subcommands {eval show hide title}

    proc title {{string {}}} {
	if {[llength [info level 0]] > 1} {
	    [conInterp] eval [list wm title . $string]
	} else {
	    return [[conInterp] eval [list wm title .]]
	}
    }

    proc hide {} {
	[conInterp] eval [list wm withdraw .]
    }

    proc show {} {
	[conInterp] eval [list wm deiconify .]
    }

    proc eval {script} {
	[conInterp] eval $script
    }

    # A command 'consoleinterp' for the console window interpreter to
    # perform commands in the application interpreter.
    namespace ensemble create -command consoleinterp \
      -subcommands {eval record} -map {eval execute}

    proc execute {cmd} {
	uplevel #0 $cmd
    }

    proc record {cmd} {
	history add $cmd
	catch {uplevel #0 $cmd} retval
	return $retval
    }

    # Channel transform for redirecting stdout/stderr
    namespace eval tkConsoleOut {
	namespace upvar [namespace parent] consoleInterp consoleInterp
	proc initialize {what x mode} {
	    fconfigure $what -buffering none -translation binary
	    info procs
	}
	proc finalize {what x} { }
	proc write {what x data} { 
	    variable consoleInterp
	    set data [string map {\r ""} $data]
	    $consoleInterp eval [list tk::ConsoleOutput $what $data]
	    if {[info exists ::TTY] && $::TTY} {return $data}
	}
	proc flush {what x} { }
	namespace export {[a-z]*}
	namespace ensemble create -parameters what
    }

    # Clean up if console widget goes away...
    proc conDelete {} {
	variable consoleInterp
	chan pop stdout	;# we hope nothing else was pushed in the meantime !
	chan pop stderr
	interp delete $consoleInterp
	set consoleInterp ""
	# puts stderr "Console closed:  check your channel transforms!"
    }

    namespace export console
}

namespace import tk::unixconsole::console
