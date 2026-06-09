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
	proc clipboard {cmd args} {
	    switch -exact -- $cmd {
		get {
		    if {![catch {exec wl-paste --no-newline} res]} {
			return $res
		    }
		    return [tcl_clipboard get {*}$args]
		}
		set - append {
		    set data [lindex $args end]
		    if {[catch {exec wl-copy << $data} err]} {
			catch {exec wl-copy -- $data}
		    }
		    return [tcl_clipboard $cmd {*}$args]
		}
		clear {
		    catch {exec wl-copy --clear}
		    tcl_clipboard clear {*}$args
		}
		default {
		    return [tcl_clipboard $cmd {*}$args]
		}
	    }
	}

	bind all <<Paste>> {
	    set _w %W
	    if {[catch {exec wl-paste --no-newline} _text]} { set _text "" }
	    if {$_text eq ""} break

	    switch -- [winfo class $_w] {
		Text {
		    if {[%W tag ranges sel] ne {}} {
			%W delete sel.first sel.last
		    }
		}
		Entry - Spinbox - Combobox {
		    if {[%W selection present]} {
			%W delete sel.first sel.last
		    }
		}
		default {
		    break
		}
	    }
	    break
	}

	bind all <<Copy>> {
	    set _w %W
	    switch -- [winfo class $_w] {
		Text {
		    if {[%W tag ranges sel] eq {}} break
		    set _text [%W get sel.first sel.last]
		    if {[catch {exec wl-copy << $_text} _err]} {
			catch {exec wl-copy -- $_text}
		    }
		}
		Entry - Spinbox - Combobox {
		    if {![%W selection present]} break
		    set _text [string range [%W get] \
				   [%W index sel.first] [%W index sel.last]]
		    if {[catch {exec wl-copy << $_text} _err]} {
			catch {exec wl-copy -- $_text}
		    }
		}
		default {
		    break
		}
	    }
	    break
	}

	bind all <<Cut>> {
	    set _w %W
	    switch -- [winfo class $_w] {
		Text {
		    if {[%W tag ranges sel] eq {}} break
		    set _text [%W get sel.first sel.last]
		    if {[catch {exec wl-copy << $_text} _err]} {
			catch {exec wl-copy -- $_text}
		    }
		    %W delete sel.first sel.last
		}
		Entry - Spinbox - Combobox {
		    if {![%W selection present]} break
		    set _text [string range [%W get] \
				   [%W index sel.first] [%W index sel.last]]
		    if {[catch {exec wl-copy << $_text} _err]} {
			catch {exec wl-copy -- $_text}
		    }
		    %W delete sel.first sel.last
		}
		default {
		    break
		}
	    }
	    break
	}
    }

    # end of Wayland commands
}
