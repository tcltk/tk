# wayland.tcl --
#
# This file includes utility functions for the Wayland port of Tcl/Tk. Wayland 
# presents a significantly different low-level API than X11, and many Tk operations
# require a different implementation strategy than other platforms. 
#
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
        rename selection  tcl_selection

        namespace eval ::tk::wayland::clip {
            variable pasteSeq      0

            # The local clipboard buffer for the CLIPBOARD selection.
            variable data   ;# data($type)   -> accumulated string
            variable fmt    ;# fmt($type)    -> format it was first appended with
            array set data {}
            array set fmt {}
            variable order    {}  ;# types, in the order first added
            variable haveOwner 0  ;# do we (this app) currently own CLIPBOARD
            variable released  0  ;# we owned CLIPBOARD, then released/cleared
            variable ownTime   0  ;# [clock milliseconds] of our last write

            # wl-paste is only ever consulted as a *fallback* when this
            # interpreter does not own the CLIPBOARD and has not just
            # released it.  That fallback must never block the event
            # loop: a vwait here would batch up redraws and make normal
            # typing appear only on Return.  We therefore never wait for
            # wl-paste to finish.  Instead we fire it off, remember a
            # short-lived cache of whatever it produced (delivered
            # asynchronously by the event loop), and return the cached
            # value if one is already available.  The first call after
            # external ownership changes returns "" and primes the
            # cache; a subsequent call can then pick it up.
            variable pasteCache     ""
            variable pasteCacheTime 0
            variable pasteInFlight  0
            variable pasteChan      ""
            variable pasteBuf       ""
        }

        # System clipboard glue (wl-copy / wl-paste).
        proc ::tk::wayland::clip::WlCopyPut {value} {
            # Redirect both stdout and stderr of the pipeline itself
            # *and* rely on wl-copy's own daemonizing so a detached
            # child (which keeps serving the selection) doesn't leak
            # stray diagnostics (e.g. mq_unlink noise from a prior,
            # already-dead clipboard holder) to our controlling
            # terminal.
            catch {
                set chan [open "|wl-copy >/dev/null 2>/dev/null" w]
                fconfigure $chan -translation binary -encoding utf-8 -blocking 0
                puts -nonewline $chan $value
                close $chan
            }
        }

        proc ::tk::wayland::clip::WlCopyClear {} {
            catch {exec wl-copy --clear >/dev/null 2>/dev/null}
            # Any cached external paste is now stale.
            variable pasteCache
            variable pasteCacheTime
            set pasteCache     ""
            set pasteCacheTime 0
        }

        # Start an asynchronous wl-paste if one is not already running.
        # Returns immediately; the result is harvested by the event loop
        # into ::tk::wayland::clip::pasteCache.
        proc ::tk::wayland::clip::WlPasteStart {} {
            variable pasteInFlight
            variable pasteChan
            variable pasteBuf

            if {$pasteInFlight} { return }

            if {[catch {open "|wl-paste --no-newline 2>/dev/null" r} chan]} {
                # Can't even start it; leave cache empty.
                return
            }
            set pasteChan $chan
            set pasteBuf  ""
            set pasteInFlight 1

            fconfigure $chan -blocking 0 -translation binary -buffering none
            fileevent $chan readable \
                    [list ::tk::wayland::clip::WlPasteReadable $chan]
        }

        proc ::tk::wayland::clip::WlPasteReadable {chan} {
            variable pasteBuf
            variable pasteInFlight
            variable pasteChan
            variable pasteCache
            variable pasteCacheTime

            if {[catch {read $chan} newdata]} { set newdata "" }
            if {$newdata ne ""} { append pasteBuf $newdata }
            if {[eof $chan]} {
                catch {fileevent $chan readable {}}
                catch {close $chan}
                set pasteChan ""

                set result $pasteBuf
                set pasteBuf ""
                if {[catch {encoding convertfrom utf-8 $result} decoded]} {
                    set decoded $result
                }
                set pasteCache     $decoded
                set pasteCacheTime [clock milliseconds]
                set pasteInFlight  0
            }
        }

        # Read the system clipboard right now.  Returns {ok text}; ok is 0
        # if wl-paste is missing, timed out, or the clipboard is empty.
        #
        # Wayland gives us no notification when another client takes the
        # CLIPBOARD away from us, so the only reliable way to notice is to
        # look at paste time.  This is called only from an explicit
        # "clipboard get", never from the event-loop path, and is bounded
        # to one second by timeout(1) so a hung clipboard owner can't
        # freeze the application.  It does not enter the event loop.
        proc ::tk::wayland::clip::WlPasteSync {} {
            set cmd "wl-paste --no-newline 2>/dev/null"
            if {[auto_execok timeout] ne ""} {
                set cmd "timeout 1 $cmd"
            }
            if {[catch {open "|$cmd" r} chan]} {
                return [list 0 ""]
            }
            # "-translation binary" alone selects raw bytes on both Tcl 8.6
            # and 9.x; "-encoding binary" was removed in Tcl 9.
            if {[catch {fconfigure $chan -translation binary} err]} {
                catch {close $chan}
                return [list 0 ""]
            }
            if {[catch {read $chan} raw]} {
                catch {close $chan}
                return [list 0 ""]
            }
            # close raises an error if the child exited non-zero.
            set ok [expr {![catch {close $chan}]}]
            if {[catch {encoding convertfrom utf-8 $raw} text]} {
                set text $raw
            }
            return [list $ok $text]
        }

        # Return whatever the most recent wl-paste produced, without
        # blocking.  Kicks off a fresh wl-paste if none is running and
        # the cache is stale.  Never waits.
        proc ::tk::wayland::clip::WlPasteGet {} {
            variable pasteCache
            variable pasteCacheTime
            variable pasteInFlight

            set now [clock milliseconds]
            # Cache lifetime: long enough that a paste following a
            # selection click sees the data, short enough that we don't
            # serve ancient contents.
            set cacheMs 1000

            if {!$pasteInFlight && ($now - $pasteCacheTime) > $cacheMs} {
                WlPasteStart
                # Fresh request is in flight; return the stale cached
                # value (possibly "") for this call.
            }
            return $pasteCache
        }

        # Small helper: match $arg against the unique-prefix
        # abbreviations Tk itself accepts for a long option name, e.g.
        # "-t"/"-ty"/"-typ"/"-type" all mean "-type". $long must include
        # the leading "-".
        proc ::tk::wayland::clip::OptMatches {arg long} {
            set alen [string length $arg]
            if {$alen < 2 || $alen > [string length $long]} { return 0 }
            return [string equal -length $alen $arg $long]
        }

        # Option parsing for "clipboard append/set"
        #   ?-displayof window? ?-format format? ?-type type? ?--? data
        # Returns a dict with keys: type format data
        #
        # Tk grammar quirks this reproduces (verified against
        # clipboard.test):
        #   * A trailing "--" with nothing after it is *data*, not an
        #     option terminator.
        #   * A trailing "-format"/"-type"/"-displayof" with no value
        #     following is likewise left as the data argument.
        #   * "--" followed by more args terminates option processing.
        #   * An option that has a value consumes both tokens; if that
        #     leaves no data argument at all, that is "wrong # args".
        proc ::tk::wayland::clip::ParseAppendArgs {argv} {
            set type STRING
            set format STRING
            set n [llength $argv]
            set i 0
            while {$i < $n} {
                set a [lindex $argv $i]
                if {[string index $a 0] ne "-"} { break }
                if {$a eq "--"} {
                    if {$i == $n - 1} {
                        # Trailing "--" is the data.
                        break
                    }
                    incr i
                    break
                }
                if {[OptMatches $a "-displayof"]} {
                    if {$i + 1 >= $n} {
                        # Dangling -displayof is the data.
                        break
                    }
                    incr i
                    set win [lindex $argv $i]
                    incr i
                    if {![winfo exists $win]} {
                        error "bad window path name \"$win\""
                    }
                } elseif {[OptMatches $a "-format"]} {
                    if {$i + 1 >= $n} {
                        # Dangling -format is the data.
                        break
                    }
                    incr i
                    set format [lindex $argv $i]
                    incr i
                } elseif {[OptMatches $a "-type"]} {
                    if {$i + 1 >= $n} {
                        # Dangling -type is the data.
                        break
                    }
                    incr i
                    set type [lindex $argv $i]
                    incr i
                } else {
                    error "bad option \"$a\": must be -displayof, -format, or -type"
                }
            }
            set rest [lrange $argv $i end]
            if {[llength $rest] != 1} {
                error "wrong # args: should be\
                        \"clipboard append ?-option value ...? data\""
            }
            return [dict create type $type format $format data [lindex $rest 0]]
        }

        # Option parsing for "clipboard get" / "selection get -selection
        # CLIPBOARD": ?-displayof window? ?-type type|type? (type may be
        # given either as "-type X"/"-t X" or as a bare trailing word,
        # both forms appear in the wild).  Same dangling-option rule as
        # ParseAppendArgs: a trailing lone option token becomes the
        # bare trailing word (here: the type).
        proc ::tk::wayland::clip::ParseGetArgs {argv} {
            set type STRING
            set n [llength $argv]
            set i 0
            while {$i < $n} {
                set a [lindex $argv $i]
                if {[string index $a 0] ne "-"} { break }
                if {$a eq "--"} {
                    if {$i == $n - 1} { break }
                    incr i
                    break
                }
                if {[OptMatches $a "-displayof"]} {
                    if {$i + 1 >= $n} { break }
                    incr i
                    set win [lindex $argv $i]
                    incr i
                    if {![winfo exists $win]} {
                        error "bad window path name \"$win\""
                    }
                } elseif {[OptMatches $a "-type"]} {
                    if {$i + 1 >= $n} { break }
                    incr i
                    set type [lindex $argv $i]
                    incr i
                } elseif {[OptMatches $a "-selection"]} {
                    # already consumed by the caller; skip its value
                    incr i 2
                } else {
                    error "bad option \"$a\": must be -displayof, -selection, or -type"
                }
            }
            set rest [lrange $argv $i end]
            if {[llength $rest] == 1} {
                set type [lindex $rest 0]
            } elseif {[llength $rest] > 1} {
                error "wrong # args: should be\
                        \"clipboard get ?-option value ...? ?type?\""
            }
            return $type
        }

        # Option parsing for "clipboard clear" / "selection clear":
        #   ?-displayof window?
        # Any trailing positional argument is a "wrong # args" error;
        # an unknown option is a "bad option ... must be -displayof";
        # a non-existent window is "bad window path name".
        proc ::tk::wayland::clip::ParseClearArgs {argv} {
            set n [llength $argv]
            set i 0
            while {$i < $n} {
                set a [lindex $argv $i]
                if {[string index $a 0] ne "-"} { break }
                if {$a eq "--"} { incr i; break }
                if {[OptMatches $a "-displayof"]} {
                    if {$i + 1 >= $n} {
                        error "wrong # args: should be\
                                \"clipboard clear ?-displayof window?\""
                    }
                    incr i
                    set win [lindex $argv $i]
                    incr i
                    if {![winfo exists $win]} {
                        error "bad window path name \"$win\""
                    }
                } else {
                    error "bad option \"$a\": must be -displayof"
                }
            }
            if {$i != $n} {
                error "wrong # args: should be\
                        \"clipboard clear ?-displayof window?\""
            }
            return
        }

        proc ::tk::wayland::clip::IsTextType {type} {
            return [expr {[string toupper $type] in
                    {STRING UTF8_STRING TEXT COMPOUND_TEXT}}]
        }

        # Buffer operations
        proc ::tk::wayland::clip::DoAppend {type format value} {
            variable data
            variable fmt
            variable order
            variable haveOwner
            variable released
            variable ownTime

            if {[info exists fmt($type)] && $fmt($type) ne $format} {
                error "format \"$format\" does not match current format\
                        \"$fmt($type)\" for $type"
            }
            if {![info exists data($type)]} {
                lappend order $type
                set data($type) $value
                set fmt($type) $format
            } else {
                append data($type) $value
            }
            set haveOwner 1
            set released 0
            set ownTime [clock milliseconds]

            if {[IsTextType $type]} {
                WlCopyPut $data($type)
            }
        }

        # Wipe the local buffer without touching the system clipboard
        # ownership flag.
        proc ::tk::wayland::clip::ResetBuffer {} {
            variable data
            variable fmt
            variable order
            array unset data
            array unset fmt
            set order {}
        }

        proc ::tk::wayland::clip::DoClear {} {
            variable haveOwner
            variable released
            ResetBuffer
            set haveOwner 0
            set released 1
            WlCopyClear
        }

        # Called by "selection own -s CLIPBOARD": take fresh ownership.
        proc ::tk::wayland::clip::DoOwn {} {
            variable data
            variable fmt
            variable order
            variable haveOwner
            variable released
            variable ownTime

            set keepString [info exists data(STRING)]
            set savedData ""
            if {$keepString} { set savedData $data(STRING) }

            ResetBuffer
            if {$keepString} {
                set data(STRING) $savedData
                set fmt(STRING)  STRING
                set order        {STRING}
            }
            set haveOwner 1
            set released 0
            set ownTime [clock milliseconds]

            if {$keepString} {
                WlCopyPut $data(STRING)
            } else {
                WlCopyClear
            }
        }

        proc ::tk::wayland::clip::DoGet {type} {
            variable data
            variable order
            variable haveOwner
            variable released
            variable ownTime

            switch -- $type {
                TARGETS {
                    return [concat {MULTIPLE TARGETS TIMESTAMP TK_APPLICATION \
                            TK_WINDOW} $order]
                }
                TK_APPLICATION {
                    return [tk appname]
                }
                TK_WINDOW {
                    return .
                }
            }

            set isText [IsTextType $type]

            # We believe we own CLIPBOARD (we copied something), but
            # Wayland never tells us when another application copies
            # afterwards.  Before trusting the local buffer, check whether
            # the system clipboard still holds what we put there; if it
            # holds something else, another client owns it now, so drop
            # our stale copy and hand back the system contents.  The grace
            # period covers the moment right after our own write, while
            # wl-copy may not have registered as the source yet.
            if {$haveOwner && $isText
                    && ([clock milliseconds] - $ownTime) > 300} {
                if {[catch {WlPasteSync} res]} { set res [list 0 ""] }
                lassign $res ok sys
                if {$ok && $sys ne ""} {
                    set local ""
                    foreach t $order {
                        if {[IsTextType $t]} {
                            set local $data($t)
                            break
                        }
                    }
                    if {$sys ne $local} {
                        ResetBuffer
                        set haveOwner 0
                        set released 0
                        return $sys
                    }
                }
            }

            if {$haveOwner && [info exists data($type)]} {
                return $data($type)
            }

            # Fall back to the system clipboard only when this
            # interpreter has never taken (and then released) ownership.
            # After an explicit clear/release, Tk semantics say the
            # selection no longer exists and "get" must error.
            if {!$haveOwner && !$released && $isText} {
                if {[catch {WlPasteSync} res]} { set res [list 0 ""] }
                lassign $res ok sys
                if {$ok && $sys ne ""} { return $sys }
            }

            error "CLIPBOARD selection doesn't exist or form \"$type\" not defined"
        }

        # Public replacements.
        proc clipboard {option args} {
            switch -- $option {
                append - set {
                    if {[catch {::tk::wayland::clip::ParseAppendArgs $args} parsed opts]} {
                        return -options $opts $parsed
                    }
                    if {$option eq "set"} {
                        ::tk::wayland::clip::DoClear
                    }
                    if {[catch {
                        ::tk::wayland::clip::DoAppend [dict get $parsed type] \
                                [dict get $parsed format] [dict get $parsed data]
                    } err opts]} {
                        return -options $opts $err
                    }
                    return {}
                }
                get {
                    if {[catch {::tk::wayland::clip::ParseGetArgs $args} type opts]} {
                        return -options $opts $type
                    }
                    if {[catch {::tk::wayland::clip::DoGet $type} res opts]} {
                        return -options $opts $res
                    }
                    return $res
                }
                clear {
                    if {[catch {::tk::wayland::clip::ParseClearArgs $args} msg opts]} {
                        return -options $opts $msg
                    }
                    ::tk::wayland::clip::DoClear
                    return {}
                }
                default {
                    return [tcl_clipboard $option {*}$args]
                }
            }
        }

        proc selection {option args} {
            # Only intercept requests that name the CLIPBOARD selection;
            # everything else (PRIMARY, SECONDARY, ...) is untouched.
            set seltype PRIMARY
            set selIndex -1
            set n [llength $args]
            for {set i 0} {$i < $n} {incr i} {
                set a [lindex $args $i]
                if {$a eq "--"} break
                if {[::tk::wayland::clip::OptMatches $a "-selection"] \
                        && $i+1 < $n} {
                    set seltype [string toupper [lindex $args [expr {$i+1}]]]
                    set selIndex $i
                    break
                }
            }

            if {$seltype ne "CLIPBOARD"} {
                return [tcl_selection $option {*}$args]
            }

            # Strip the -selection <name> pair before handing to our
            # option parsers, which don't know about it.
            if {$selIndex >= 0} {
                set stripped [lreplace $args $selIndex [expr {$selIndex+1}]]
            } else {
                set stripped $args
            }

            switch -- $option {
                get {
                    if {[catch {::tk::wayland::clip::ParseGetArgs $stripped} type opts]} {
                        return -options $opts $type
                    }
                    if {[catch {::tk::wayland::clip::DoGet $type} res opts]} {
                        return -options $opts $res
                    }
                    return $res
                }
                own {
                    # CLIPBOARD ownership is tracked locally (see
                    # ::tk::wayland::clip::haveOwner).  Taking ownership
                    # discards any previously-appended non-default type
                    # slots, and we deliberately do *not* forward to the
                    # native/GLFW own path, which is the one that can
                    # hang on Wayland.
                    if {[llength $stripped] > 1} {
                        return -code error \
                            "wrong # args: should be \"selection own ?-selection selection? ?window?\""
                    }
                    ::tk::wayland::clip::DoOwn
                    return {}
                }
                clear {
                    if {[catch {::tk::wayland::clip::ParseClearArgs $stripped} msg opts]} {
                        return -options $opts $msg
                    }
                    ::tk::wayland::clip::DoClear
                    return {}
                }
                handle {
                    # Not meaningful without real selection ownership;
                    # accepted as a no-op for script compatibility.
                    return {}
                }
                default {
                    return [tcl_selection $option {*}$args]
                }
            }
        }

        encoding system utf-8
    }
    # end of Wayland commands
}
