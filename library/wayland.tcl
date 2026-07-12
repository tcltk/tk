# wayland.tcl --

# This file includes utility functions for the Wayland port of Tcl/Tk. Wayland 
# presents a significantly different low-level API than X11, and many Tk operations
# require a different implementation strategy than other platforms. 

# Copyright (c) 2026 Kevin Walzer
#
# See the file "license.terms" for information on usage and redistribution
# of this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
    
if {[tk windowingsystem] eq "wayland"} {	

    # Rename the "clipboard" command to add support for the Wayland system
    # clipboard via the wl-clipboard command line tools. GLFW's clipboard
    # support on Wayland is essentially broken, so integration at the
    # C level is basically impossible. The script-level integration
    # works very well.
    proc rename_wayland_clipboard {} {
	rename clipboard tcl_clipboard
	rename selection tcl_selection

	# wayland_clipboard_get --
	#
	#	Read the system clipboard via wl-paste, without blocking the
	#	Tcl event loop indefinitely. wl-paste can hang -- e.g. when
	#	no clipboard-owning client is reachable via the compositor --
	#	so the read runs through a non-blocking pipe with a timeout;
	#	if the timeout fires, the wl-paste process is killed and an
	#	empty result is returned instead of freezing the UI.
	#
	#	Raises an error only if wl-paste could not be spawned at all
	#	(e.g. not installed), so callers can fall back to the
	#	original Tk selection mechanism in that case. A hang or a
	#	genuinely empty clipboard both simply return "".
	proc wayland_clipboard_get {{timeout_ms 500}} {
	    if {[catch {open "|wl-paste --no-newline" r} chan]} {
		return -code error "wl-paste unavailable"
	    }
	    fconfigure $chan -blocking 0 -translation binary -buffering none

	    set ::wayland_paste_buf($chan) ""
	    set ::wayland_paste_done($chan) 0

	    fileevent $chan readable [list apply {{chan} {
		if {[catch {read $chan} data]} {
		    set data ""
		}
		append ::wayland_paste_buf($chan) $data
		if {[eof $chan]} {
		    set ::wayland_paste_done($chan) 1
		}
	    }} $chan]

	    set timer [after $timeout_ms [list apply {{chan} {
		if {[info exists ::wayland_paste_done($chan)] \
			&& !$::wayland_paste_done($chan)} {
		    catch {exec kill [lindex [pid $chan] 0]}
		    set ::wayland_paste_done($chan) 1
		}
	    }} $chan]]

	    vwait ::wayland_paste_done($chan)

	    after cancel $timer
	    catch {fileevent $chan readable {}}
	    set result $::wayland_paste_buf($chan)
	    unset -nocomplain ::wayland_paste_buf($chan)
	    unset -nocomplain ::wayland_paste_done($chan)
	    catch {close $chan}
	    return $result
	}

	# wayland_clipboard_put --
	#
	#	Write text to the system clipboard via wl-copy, backgrounded
	#	(trailing "&") so a stuck or slow wl-copy cannot block the
	#	event loop either. wl-copy normally forks and detaches on its
	#	own to persist as the selection owner, but backgrounding here
	#	means Tcl never waits on it regardless.
	proc wayland_clipboard_put {data} {
	    if {[catch {exec wl-copy << $data &} err]} {
		catch {exec wl-copy -- $data &}
	    }
	}

	proc clipboard {cmd args} {
	    switch -exact -- $cmd {
		get {
		    if {[catch {wayland_clipboard_get} res]} {
			return [tcl_clipboard get {*}$args]
		    }
		    return $res
		}
		set - append {
		    set data [lindex $args end]
		    wayland_clipboard_put $data
		    return [tcl_clipboard $cmd {*}$args]
		}
		clear {
		    catch {exec wl-copy --clear &}
		    tcl_clipboard clear {*}$args
		}
		default {
		    return [tcl_clipboard $cmd {*}$args]
		}
	    }
	}

	# The built-in "selection" command is also wrapped: "selection get
	# -selection CLIPBOARD" previously fell straight through to Tk's
	# internal (GLFW-backed) selection retrieval, which is broken on
	# Wayland and silently returns an empty string with a success
	# code. Only the CLIPBOARD selection is redirected through
	# wl-paste; PRIMARY and any other selection are left untouched.
	proc selection {cmd args} {
	    switch -exact -- $cmd {
		get {
		    set idx [lsearch -exact $args "-selection"]
		    if {$idx >= 0 \
			    && [lindex $args [expr {$idx + 1}]] eq "CLIPBOARD"} {
			if {![catch {wayland_clipboard_get} res]} {
			    return $res
			}
		    }
		    return [tcl_selection get {*}$args]
		}
		default {
		    return [tcl_selection $cmd {*}$args]
		}
	    }
	}

	# No <<Copy>>/<<Cut>>/<<Paste>> bindings here. Tk's own library
	# bindings for Entry/Text (and friends) already call "clipboard"
	# and "selection" under the hood, and those are the commands
	# wrapped above. Adding bind all <<...>> scripts on top of that
	# just raced against the native class bindtag bindings -- which
	# run first and end in "break" -- so the custom scripts never
	# fired, while the *native* bindings, now backed by working
	# wl-copy/wl-paste, did the real work and were the actual source
	# of the keyboard-shortcut/selection-state problems. Wrapping at
	# the command level is sufficient on its own.
    }
    # end of Wayland commands
}
