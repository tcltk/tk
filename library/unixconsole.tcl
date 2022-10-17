# FILE: console.tcl
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

set ::tk::console_on_unix {
    ########################################################################
    # If the Tcl command 'console' is already in the interpreter, our work
    # is done.
    ########################################################################
    if {![catch {console show}]} {
	return
    }

    ########################################################################
    # Check Tcl/Tk support
    ########################################################################
    package require Tcl 8.6-
    package require Tk 8

    apply {{} {
	global tk_library
	set conimpl [file join $tk_library console.tcl]
	if {![file readable $conimpl]} {
	    return -code error "File not readable: $conimpl"
	}
    }}

    ########################################################################
    # Provide the support which the Tk library script console.tcl assumes
    ########################################################################
    # 1. Create an interpreter for the console window widget and load Tk
    set consoleInterp [interp create]
    $consoleInterp eval [list set tk_library $tk_library]
    $consoleInterp alias exit exit
    load "" Tk $consoleInterp

    # 2. A command 'console' in the application interpreter
    ;proc console {sub {optarg {}}} [subst -nocommands {
	switch -exact -- \$sub {
	    title {
		$consoleInterp eval wm title . [list \$optarg]
	    }
	    hide {
		$consoleInterp eval wm withdraw .
	    }
	    show {
		$consoleInterp eval wm deiconify .
	    }
	    eval {
		$consoleInterp eval \$optarg
	    }
	    default {
		error "bad option \\\"\$sub\\\": should be hide, show, or title"
	    }
	}
    }]

    # 3. Alias a command 'consoleinterp' in the console window interpreter
    #       to cause evaluation of the command 'consoleinterp' in the
    #       application interpreter.
    ;proc consoleinterp {sub cmd} {
	switch -exact -- $sub {
	    eval {
		uplevel #0 $cmd
	    }
	    record {
		history add $cmd
		catch {uplevel #0 $cmd} retval
		return $retval
	    }
	    default {
		error "bad option \"$sub\": should be eval or record"
	    }
	}
    }
    if {[package vsatisfies [package provide Tk] 4]} {
	$consoleInterp alias interp consoleinterp
    } else {
	$consoleInterp alias consoleinterp consoleinterp
    }

    # 4. Bind the <Destroy> event of the application interpreter's main
    #    window to kill the console (via tkConsoleExit)
    bind . <Destroy> [list +if {[string match . %W]} [list catch \
	[list $consoleInterp eval tkConsoleExit]]]

    # 5. Redirect stdout/stderr messages to the console
    if {[package vcompare [package present Tcl] 8.6] >= 0} {
	# 5a. we can use TIP#230 channel transforms to achieve this simply:
	namespace eval tkConsoleOut {
	    variable consoleInterp $::consoleInterp
	    proc initialize {what x mode}    {
		fconfigure $what -buffering none -translation binary
		info procs
	    }
	    proc finalize {what x }          { }
	    proc write {what x data}         {
		variable consoleInterp
		set data [string map {\r ""} $data]
		$consoleInterp eval [list tkConsoleOutput $what $data]
		if {[info exists ::TTY] && $::TTY} {return $data}
	    }
	    proc flush {what x}              { }
	    namespace export {[a-z]*}
	    namespace ensemble create -parameters what
	}
	chan push stdout {::tkConsoleOut stdout}
	chan push stderr {::tkConsoleOut stderr}
	# Restore normal [puts] if console widget goes away...
	proc Oc_RestorePuts {slave} {
	    chan pop stdout     ;# we hope nothing else was pushed in the meantime !
	    chan pop stderr
	    puts stderr "Console closed:  check your channel transforms!"
	}
    } else {
	# 5b. Pre-8.6 needs to redefine 'puts' in order to redirect stdout
	#     and stderr messages to the console
	rename puts tcl_puts
	;proc puts {args} [subst -nocommands {
	    switch -exact -- [llength \$args] {
		1 {
		    if {[string match -nonewline \$args]} {
			if {[catch {uplevel 1 [linsert \$args 0 tcl_puts]} msg]} {
			    regsub -all tcl_puts \$msg puts msg
			    return -code error \$msg
			}
		    } else {
			$consoleInterp eval [list \
			    tkConsoleOutput stdout "[lindex \$args 0]\n"]
		    }
		}
		2 {
		    if {[string match -nonewline [lindex \$args 0]]} {
			$consoleInterp eval [list \
			    tkConsoleOutput stdout [lindex \$args 1]]
		    } elseif {[string match stdout [lindex \$args 0]]} {
			$consoleInterp eval [list \
			    tkConsoleOutput stdout "[lindex \$args 1]\n"]
		    } elseif {[string match stderr [lindex \$args 0]]} {
			$consoleInterp eval [list \
			    tkConsoleOutput stderr "[lindex \$args 1]\n"]
		    } else {
			if {[catch {uplevel 1 [linsert \$args 0 tcl_puts]} msg]} {
			    regsub -all tcl_puts \$msg puts msg
			    return -code error \$msg
			}
		    }
		}
		3 {
		    if {![string match -nonewline [lindex \$args 0]]} {
			if {[catch {uplevel 1 [linsert \$args 0 tcl_puts]} msg]} {
			    regsub -all tcl_puts \$msg puts msg
			    return -code error \$msg
			}
		    } elseif {[string match stdout [lindex \$args 1]]} {
			$consoleInterp eval [list \
			    tkConsoleOutput stdout [lindex \$args 2]]
		    } elseif {[string match stderr [lindex \$args 1]]} {
			$consoleInterp eval [list \
			    tkConsoleOutput stderr [lindex \$args 2]]
		    } else {
			if {[catch {uplevel 1 [linsert \$args 0 tcl_puts]} msg]} {
			    regsub -all tcl_puts \$msg puts msg
			    return -code error \$msg
			}
		    }
		}
		default {
		    if {[catch {uplevel 1 [linsert \$args 0 tcl_puts]} msg]} {
			regsub -all tcl_puts \$msg puts msg
			return -code error \$msg
		    }
		}
	    }
	}]
	$consoleInterp alias puts puts
	# Restore normal [puts] if console widget goes away...
	proc Oc_RestorePuts {slave} {
	    rename puts {}
	    rename tcl_puts puts
	    interp delete $slave
	}
    }

    # 6. No matter what Tk_Main says, insist that this is an interactive  shell
    set tcl_interactive 1

    ########################################################################
    # Evaluate the Tk library script console.tcl in the console interpreter
    ########################################################################
    $consoleInterp eval source [list [file join $tk_library console.tcl]]
    $consoleInterp eval {
	if {![llength [info commands tkConsoleExit]]} {
	    tk::unsupported::ExposePrivateCommand tkConsoleExit
	}
    }
    $consoleInterp eval {
	if {![llength [info commands tkConsoleOutput]]} {
	    tk::unsupported::ExposePrivateCommand tkConsoleOutput
	}
    }
    if {[string match 8.3.4 $tk_patchLevel]} {
	# Workaround bug in first draft of the tkcon enhancments
	$consoleInterp eval {
	    bind Console <Control-Key-v> {}
	}
    }

    $consoleInterp alias Oc_RestorePuts Oc_RestorePuts $consoleInterp
    $consoleInterp eval {
	bind Console <Destroy> +Oc_RestorePuts
    }

    # addition by Schelte Bron ([sbron]):
    # Allow functional pasting with the middle mouse button
    catch {
	# on particularly old Tk versions, virtual events might not be present?
	# FIXME: this should be guarded with an appropriate version test
	$consoleInterp eval {
	    bind Console <<PasteSelection>> {
		if {$tk_strictMotif || ![info exists tk::Priv(mouseMoved)] \
			|| !$tk::Priv(mouseMoved)} {
		    catch {
			set clip [::tk::GetSelection %W PRIMARY]
			set list [split $clip \n\r]
			tk::ConsoleInsert %W [lindex $list 0]
			foreach x [lrange $list 1 end] {
			    %W mark set insert {end - 1c}
			    tk::ConsoleInsert %W "\n"
			    tk::ConsoleInvoke
			    tk::ConsoleInsert %W $x
			}
		    }
		}
	    }
	}
    }

    unset consoleInterp
    console title "[wm title .] Console"

    set ::tk::console_on_unix_begin 1 ;# extra statement to continue after vwait
}
proc console {args} { ;# initial one time use definition of console
    rename console {} ;# delete this definition of console now
    after 0 {eval $::tk::console_on_unix ; unset -nocomplain ::tk::console_on_unix}
    vwait ::tk::console_on_unix_begin ;# wait till setup complete
    unset -nocomplain ::tk::console_on_unix_begin
    tailcall console {*}$args ;# call the new console command with current args
}
