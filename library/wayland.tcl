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

        # Non-blocking wl-paste with hard timeout - avoids open | blocking.
        proc wayland_clipboard_get {{timeout_ms 250}} {
            # Use try to spawn wl-paste; if it hangs, the pipeline open will still return
            # but read will be handled async.
            if {[catch {set chan [open "|wl-paste --no-newline 2>/dev/null" r]}]} {
                return -code error "wl-paste unavailable"
            }
            # Make non-blocking BEFORE any read.
            fconfigure $chan -blocking 0 -translation binary -buffering none -encoding binary

            set ::wayland_paste_buf($chan) ""
            set ::wayland_paste_done($chan) 0
            set ::wayland_paste_timer($chan) ""

            fileevent $chan readable [list apply {{chan} {
                if {[catch {set data [read $chan]}]} {set data ""}
                if {$data ne ""} {append ::wayland_paste_buf($chan) $data}
                if {[eof $chan]} {
                    set ::wayland_paste_done($chan) 1
                }
            }} $chan]

            # Timeout kills wl-paste.
            set ::wayland_paste_timer($chan) [after $timeout_ms [list apply {{chan} {
                if {[info exists ::wayland_paste_done($chan)] && !$::wayland_paste_done($chan)} {
                    catch {close $chan}
                    set ::wayland_paste_done($chan) 1
                }
            }} $chan]]

            # vwait with protection against nested vwait deadlock:
            # if we are already in a vwait (pause uses vwait), use update loop instead of vwait.
            set start [clock milliseconds]
            while {!$::wayland_paste_done($chan)} {
                # Process events for up to 10ms, then check timeout.
                if {[catch {vwait ::wayland_paste_done($chan)}]} {
                    # Nested vwait failed, fallback to update.
                    update
                }
                if {[clock milliseconds] - $start > $timeout_ms} {
                    catch {close $chan}
                    set ::wayland_paste_done($chan) 1
                    break
                }
            }

            catch {after cancel $::wayland_paste_timer($chan)}
            catch {fileevent $chan readable {}}
            set result ""
            if {[info exists ::wayland_paste_buf($chan)]} {
                set result $::wayland_paste_buf($chan)
            }
            foreach v [list ::wayland_paste_buf($chan) ::wayland_paste_done($chan) ::wayland_paste_timer($chan)] {
                unset -nocomplain $v
            }
            catch {close $chan}

            # Decode UTF-8.
            if {[catch {set result [encoding convertfrom utf-8 $result]}]} {
                catch {set result [encoding convertfrom [encoding system] $result]}
            }
            return $result
        }

        proc wayland_clipboard_put {data} {
            # Avoid passing huge data via exec arg list - use stdin.
            # Backgrounded, never wait.
            if {[catch {
                set chan [open "|wl-copy 2>/dev/null" w]
                fconfigure $chan -translation binary -encoding utf-8 -buffering none
                puts -nonewline $chan $data
                close $chan
            }]} {
                catch {exec wl-copy -- $data &}
            }
        }

        proc clipboard {cmd args} {
            switch -exact -- $cmd {
                get {
                    # On Wayland, tcl_clipboard get is broken and can hang in GLFW.
                    # Try wl-paste first with short timeout, fallback only if wl-paste fails to spawn.
                    if {[catch {wayland_clipboard_get 200} res] == 0} {
                        return $res
                    }
                    # Fallback - but wrap in timeout to avoid hang.
                    if {[catch {after 300 set ::clip_fallback_done 1; set ::clip_fallback_done 0; vwait ::clip_fallback_done; tcl_clipboard get {*}$args} res]} {
                        return ""
                    }
                    return $res
                }
                set - append {
                    set data [lindex $args end]
                    wayland_clipboard_put $data
                    # DO NOT call tcl_clipboard set on Wayland - GLFW impl hangs trying to own Wayland selection.
                    return $data
                }
                clear {
                    catch {exec wl-copy --clear &}
                    catch {tcl_clipboard clear {*}$args}
                    return ""
                }
                default {
                    return [tcl_clipboard $cmd {*}$args]
                }
            }
        }
        

	proc selection {cmd args} {
	    switch -exact -- $cmd {
		get {
		    set idx [lsearch -exact $args "-selection"]
		    set seltype "PRIMARY"
		    if {$idx >= 0} {set seltype [string toupper [lindex $args [expr {$idx+1}]]]}
		    
		    if {$seltype eq "CLIPBOARD"} {
			if {![catch {wayland_clipboard_get 150} res] && $res ne ""} {
			    return $res
			}
			return ""
		    }
		    # PRIMARY: never go to Wayland / tcl_selection get - it blocks in C.
		    # Use the widget that owns the selection directly.
		    if {[catch {set owner [tcl_selection own -selection PRIMARY]}]} {set owner ""}
		    if {$owner ne "" && [winfo exists $owner]} {
			set cls [winfo class $owner]
			if {$cls eq "Text"} {
			    if {[catch {$owner get sel.first sel.last} r]==0} {return $r}
			} elseif {$cls in {Entry TEntry TCombobox Spinbox}} {
			    if {![catch {
				set txt [$owner get]
				set s [$owner index sel.first]
				set e [$owner index sel.last]
				set r [string range $txt $s [expr {$e-1}]]
			    }]} {return $r}
			} elseif {$cls eq "Listbox"} {
			    if {![catch {$owner curselection} idxs]} {
				set out {}
				foreach i $idxs {lappend out [$owner get $i]}
				return [join $out "\n"]
			    }
			}
		    }
		    # No owner or can't read - return "" instead of blocking.
		    return ""
		}
		own {
		    set idx [lsearch -exact $args "-selection"]
		    set seltype "PRIMARY"
		    if {$idx >= 0} {set seltype [string toupper [lindex $args [expr {$idx+1}]]]}
		    if {$seltype eq "CLIPBOARD"} {
			# CLIPBOARD ownership is handled via wl-copy in clipboard set, not here.
			catch {tcl_selection own {*}$args}
			return -code error "CLIPBOARD own handled via clipboard command"
		    }
		    return [tcl_selection own {*}$args]
		}
		clear - handle {
		    # PRIMARY clear/handle stays internal.
		    set idx [lsearch -exact $args "-selection"]
		    set seltype "PRIMARY"
		    if {$idx >= 0} {set seltype [string toupper [lindex $args [expr {$idx+1}]]]}
		    if {$seltype eq "CLIPBOARD"} {
			catch {tcl_selection $cmd {*}$args}
			return ""
		    }
		    return [tcl_selection $cmd {*}$args]
		}
		default {
		    # For PRIMARY own/clear/handle keep internal.
		    set idx [lsearch -exact $args "-selection"]
		    set seltype "PRIMARY"
		    if {$idx >= 0} {set seltype [string toupper [lindex $args [expr {$idx+1}]]]}
		    if {$seltype eq "CLIPBOARD"} {
			if {[catch {tcl_selection $cmd {*}$args} res]} {return ""}
			return $res
		    }
		    return [tcl_selection $cmd {*}$args]
		}
	    }
	}

        encoding system utf-8
    }
    # end of Wayland commands
}
