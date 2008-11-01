#
# $Id: spinbox.tcl,v 1.1 2008/11/01 15:34:24 patthoyts Exp $
#
# Tile widget set: spinbox bindings.
#
#

namespace eval ttk::spinbox {
    variable Values	;# Values($cb) is -listvariable of listbox widget

    variable State
    set State(entryPress) 0
}

### Spinbox bindings.
#
# Duplicate the Entry bindings, override if needed:
#

ttk::copyBindings TEntry TSpinbox

bind TSpinbox <Double-Button-1> {ttk::spinbox::Select %W %x %y word}
bind TSpinbox <Triple-Button-1> {ttk::spinbox::Select %W %x %y line}

bind TSpinbox <ButtonPress-1> { ttk::spinbox::Press %W %x %y }
bind TSpinbox <ButtonRelease-1> { ttk::spinbox::Release %W %x %y }
bind TSpinbox <MouseWheel> {ttk::spinbox::Change %W [expr {%D/-120}] line}
bind TSpinbox <Up> {ttk::spinbox::Change %W +[%W cget -increment] line}
bind TSpinbox <Down> {ttk::spinbox::Change %W -[%W cget -increment] line}


proc ttk::spinbox::Press {w x y} {
    if {[$w instate disabled]} { return }
    variable State
    set State(xPress) $x
    set State(yPress) $y
    focus $w
    switch -glob -- [$w identify $x $y] {
        *uparrow {
            ttk::Repeatedly Change $w +[$w cget -increment] line
        }
        *downarrow {
            ttk::Repeatedly Change $w -[$w cget -increment] line
        }
        *textarea {
            set State(entryPress) [$w instate !readonly]
            if {$State(entryPress)} {
                ttk::entry::Press $w $x
            }
        }
    }
}

proc ttk::spinbox::Release {w x y} {
    variable State
    unset -nocomplain State(xPress) State(yPress)
    ttk::CancelRepeat
}

proc ttk::spinbox::Change {w n units} {
    if {[set vlen [llength [$w cget -values]]] != 0} {
        set index [expr {[$w current] + $n}]
        if {[catch {$w current $index}]} {
            if {[$w cget -wrap]} {
                if {$index == -1} {
                    set index [llength [$w cget -values]]
                    incr index -1
                } else {
                    set index 0
                }
                $w current $index
            }
        }
    } else {
        if {![catch {expr {[$w get] + $n}} v]} {
            if {$v < [$w cget -from]} {
                if {[$w cget -wrap]} {
                    set v [$w cget -to]
                } else {
                    set v [$w cget -from]
                }
            } elseif {$v > [$w cget -to]} {
                if {[$w cget -wrap]} {
                    set v [$w cget -from]
                } else {
                    set v [$w cget -to]
                }
            }
            $w set $v
        }
    }
    ::ttk::entry::Select $w 0 $units

    # Run -command callback:
    #
    uplevel #0 [$w cget -command]

}

# Spinbox double-click on the arrows needs interception, otherwise
# pass to the TEntry handler
proc ttk::spinbox::Select {w x y mode} {
    if {[$w instate disabled]} { return }
    variable State
    set State(xPress) $x
    set State(yPress) $y
    switch -glob -- [$w identify $x $y] {
        *uparrow {
            ttk::Repeatedly Change $w +[$w cget -increment] units
        }
        *downarrow {
            ttk::Repeatedly Change $w -[$w cget -increment] units
        }
        *textarea { 
            return [::ttk::entry::Select $w $x $mode]
        }
    }
    return -code continue
}

#*EOF*
