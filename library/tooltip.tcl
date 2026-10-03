# tooltip.tcl - Simple cross-platform tooltip module
#
# Wayland: a shared one-item menu is post'ed on <Enter> (after a short
# delay) and unpost'ed on <Leave>/<ButtonPress>/<KeyPress>.  No grab, no
# focus changes, no per-widget binding rewrites.
#
# Other platforms: override-redirect toplevel, with platform-specific flags
# as needed. 

namespace eval ::tk::tooltip {
    variable _text                      ;# widget -> tooltip text
    array set _text {}
    variable _current ""                ;# widget whose tooltip is showing
    variable _after_id ""
    variable _prev_focus ""             ;# non-Wayland only
    variable _px 0                      ;# last pointer position, from
    variable _py 0                      ;# <Enter>/<Motion> %X %Y
    variable debug 0                    ;# set ::tk::tooltip::debug 1 to trace
    variable _delay 300                 ;# hover delay before showing (ms)
    variable _menu .__tooltip_menu__    ;# shared Wayland popup
    variable _toplevel .__tooltip__     ;# shared non-Wayland popup

    namespace export tooltip
}

# ::tk::tooltip::tooltip --
# Set, change or (with an empty string) remove a widget's tooltip.
# Usage: tk tooltip .widget "tooltip text"
proc ::tk::tooltip::tooltip {widget text} {
    variable _text
    variable _current

    if {![winfo exists $widget]} {
        return -code error "widget \"$widget\" does not exist"
    }

    set tags [bindtags $widget]
    set idx  [lsearch -exact $tags Tooltip]

    if {$text eq ""} {
        unset -nocomplain _text($widget)
        if {$idx >= 0} {
            bindtags $widget [lreplace $tags $idx $idx]
        }
        if {$_current eq $widget} {
            _hide
        }
        return
    }

    set _text($widget) $text
    if {$idx < 0} {
        bindtags $widget [linsert $tags 0 Tooltip]
    }
}

# Bindings live on the "Tooltip" bindtag, defined once.  Nothing here
# touches the widget's own bindings, and the tooltip text never goes
# through %-substitution.
bind Tooltip <Enter>       {::tk::tooltip::_enter %W %m %X %Y}
bind Tooltip <Motion>      {::tk::tooltip::_motion %W %X %Y}
bind Tooltip <Leave>       {::tk::tooltip::_leave %W %m}
bind Tooltip <ButtonPress> {::tk::tooltip::_hide}
bind Tooltip <KeyPress>    {::tk::tooltip::_hide}
bind Tooltip <Destroy>     {::tk::tooltip::_destroyed %W}

# Private: <Enter> - schedule the tooltip.
proc ::tk::tooltip::_enter {widget mode x y} {
    variable _after_id
    variable _delay
    variable _text
    variable _px
    variable _py

    _dbg "enter $widget mode=$mode at $x,$y"

    # Crossing events caused by grabs (e.g. another popup closing) are
    # not the user moving the pointer onto the widget.
    if {$mode in {NotifyGrab NotifyUngrab}} return

    set _px $x
    set _py $y
    _cancel
    if {[info exists _text($widget)]} {
        set _after_id [after $_delay \
            [list ::tk::tooltip::_show $widget]]
    }
}

# Private: <Leave>
proc ::tk::tooltip::_leave {widget mode} {
    _dbg "leave $widget mode=$mode"
    _hide
}

# Private: <Motion> - keep the pointer position current.  (Wayland has
# no usable global pointer query; event coordinates are.)  If we are
# showing a tooltip for a different widget than the one now under the
# pointer, the <Leave>/<Enter> pair was lost: switch tooltips here.
proc ::tk::tooltip::_motion {widget x y} {
    variable _px
    variable _py
    variable _current
    variable _after_id
    variable _delay
    variable _text

    set _px $x
    set _py $y

    if {$_current eq ""} return

    _dbg "motion $widget at $x,$y (tooltip showing for $_current)"
    if {$widget ne $_current} {
        _hide
        if {[info exists _text($widget)]} {
            set _after_id [after $_delay \
                [list ::tk::tooltip::_show $widget]]
        }
    }
}

# Private: debug trace.
proc ::tk::tooltip::_dbg {msg} {
    variable debug

    if {$debug} {
        puts stderr "tooltip: $msg"
    }
}

# Private: <Destroy>
proc ::tk::tooltip::_destroyed {widget} {
    variable _text
    variable _current

    unset -nocomplain _text($widget)
    if {$_current eq $widget} {
        _hide
    }
}

# Private: cancel a pending show.
proc ::tk::tooltip::_cancel {} {
    variable _after_id

    if {$_after_id ne ""} {
        after cancel $_after_id
        set _after_id ""
    }
}

# Private: timer fired.  <Leave> cancels the timer, so no "is the
# pointer still here?" check is needed.
proc ::tk::tooltip::_show {widget} {
    variable _after_id
    variable _current
    variable _text
    variable _px
    variable _py

    set _after_id ""

    _dbg "show $widget (event pos $_px,$_py; pointerxy [winfo pointerxy $widget])"
    if {![winfo exists $widget] || ![info exists _text($widget)]} return
    if {$_current eq $widget} return
    _hide

    if {[tk windowingsystem] eq "wayland"} {
        _show_menu $_text($widget) $_px $_py
    } else {
        _show_toplevel $widget $_text($widget)
    }
    set _current $widget
}

# Private: Wayland - a shared, one-item, non-interactive menu.
#
# Uses "post", not tk_popup: tk_popup grabs, which would swallow the
# click that should reach the widget, redirect crossing events, and
# generate a spurious <Leave> on the source widget.
proc ::tk::tooltip::_show_menu {text px py} {
    variable _menu

    # Always build a fresh menu; _hide destroys it (see there).
    catch {destroy $_menu}
    # Give tooltips a different appearance from regular menus.
    menu $_menu -tearoff 0 \
        -background black -foreground white \
        -activebackground black -activeforeground white \
        -activeborderwidth 5
    $_menu add command
    $_menu entryconfigure 0 -label $text

    # Offset so the pointer is not on the popup.
    _dbg "post $_menu at [expr {$px + 10}],[expr {$py + 15}]"
    $_menu post [expr {$px + 10}] [expr {$py + 15}]
}

# Private: non-Wayland - override-redirect toplevel.
proc ::tk::tooltip::_show_toplevel {widget text} {
    variable _prev_focus
    variable _toplevel

    set _prev_focus [focus]

    set w $_toplevel
    if {[winfo exists $w]} {
        destroy $w
    }

    toplevel $w -bd 1 -relief solid -bg lightyellow
    wm overrideredirect $w true
    wm state $w withdrawn

    switch -- [tk windowingsystem] {
        aqua {
            update idletasks
            wm attributes $w -stylemask docmodal
        }
        win32 {
            wm attributes $w -topmost 1
        }
    }

    message $w.msg -text $text -aspect 10000 -bg lightyellow -fg black
    pack $w.msg -padx 3 -pady 2

    # Below the widget, centered horizontally, kept on screen.
    update idletasks
    set rw [winfo reqwidth $w]
    set rh [winfo reqheight $w]
    set sw [winfo screenwidth $w]
    set sh [winfo screenheight $w]

    set x [expr {[winfo rootx $widget] + ([winfo width $widget] - $rw) / 2}]
    set y [expr {[winfo rooty $widget] + [winfo height $widget] + 5}]

    if {($x + $rw) > $sw} {set x [expr {$sw - $rw - 5}]}
    if {$x < 0}           {set x 5}
    if {($y + $rh) > $sh} {set y [expr {[winfo rooty $widget] - $rh - 5}]}
    if {$y < 0}           {set y 5}

    wm geometry $w +$x+$y
    wm deiconify $w
    raise $w

    # On Aqua, restore focus after showing to keep inputs responsive.
    if {[tk windowingsystem] eq "aqua"} {
        after idle [list focus -force [focus]]
    }
}

# Private: hide whatever is showing (and cancel anything pending).
proc ::tk::tooltip::_hide {} {
    variable _current
    variable _menu
    variable _toplevel
    variable _prev_focus

    _cancel

    if {$_current eq ""} return
    set _current ""

    if {[winfo exists $_menu]} {
        # Destroy rather than just unpost: destroying is what tears down
        # the wl_subsurface.  Unpost alone only unmaps the Tk window.
        catch {$_menu unpost}
        catch {grab release $_menu}
        destroy $_menu
    }

    if {[winfo exists $_toplevel]} {
        destroy $_toplevel
    }

    if {$_prev_focus ne ""} {
        if {[winfo exists $_prev_focus]} {
            catch {focus -force $_prev_focus}
        }
        set _prev_focus ""
    }
}

# Add to tk ensemble.
namespace ensemble configure tk -map \
    [dict merge [namespace ensemble configure tk -map] \
        {tooltip ::tk::tooltip::tooltip}]
