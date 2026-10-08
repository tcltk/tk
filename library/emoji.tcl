# emoji.tcl -- simplified Tk emoji chooser for Linux (X11 and Wayland)
#
# A pure Tk dialog; no native code needed.
#  - Uses monochrome fonts: Noto Emoji and similar
#  - Bound to Control-. / Control-; and Super-. / Super-; (Mod4) on text,
#    entry and ttk::entry
#  - Popup over the widget, insert at insertion cursor
#
# Does nothing on Windows and macOS, which have their own system pickers.

package require Tk 9.0-

# Bail if X11 and compiled without Xft support
if {[tk::build-info no-xft]} {
	bgerror "Modern font support such as Xft required for emojis"
}

namespace eval ::tk::emoji {
    variable S
    array set S {
        top    .tkemojipicker
        cell   26
        cat    ""
        cur    -1
        cols   1
        items  {}
        drawnW -1
    }
    variable recent {}
    variable maxRecent 24

    variable categories {
        Recent Smileys Gestures People Animals Food Travel Activities
        Objects Symbols
    }
    variable icons [dict create \
        Recent \u263A Smileys \u263A Gestures \u270B People \u263A \
        Animals \u2766 Food \u2615 Travel \u2708 Activities \u26BD \
        Objects \u2692 Symbols \u2665]

    # Only code points with reliable monochrome coverage in Noto Emoji,
    # Noto Sans Symbols / Symbols2, Symbola, or DejaVu Sans. Newer
    # Unicode 12+/13+/14+ emoji that only exist in Noto Color Emoji
    # have been omitted so they don't fall back to a color font.
    variable data [dict create \
    Smileys {
        \u263A \u263B \U1F600 \U1F603 \U1F604 \U1F601 \U1F606 \U1F602 \U1F642 \U1F643
        \U1F609 \U1F60A \U1F607 \U1F60D \U1F618 \U1F617 \U1F61A \U1F619
        \U1F60B \U1F61B \U1F61C \U1F61D \U1F911 \U1F917 \U1F914
        \U1F910 \U1F610 \U1F611 \U1F636 \U1F60F \U1F612 \U1F644 \U1F62C
        \U1F60C \U1F614 \U1F62A \U1F634 \U1F637 \U1F912 \U1F915 \U1F922
        \U1F927 \U1F635 \U1F920 \U1F60E \U1F913
        \U1F615 \U1F61F \U1F641 \U1F62E \U1F62F \U1F632 \U1F633 \U1F626
        \U1F627 \U1F628 \U1F630 \U1F625 \U1F622 \U1F62D \U1F631 \U1F616 \U1F623 \U1F61E
        \U1F613 \U1F629 \U1F62B \U1F624 \U1F621 \U1F620 \U1F608 \U1F47F
        \U1F480 \u2620 \U1F4A9 \U1F47B \U1F47D \U1F916
    } \
    Gestures {
        \U1F44B \U1F590 \u270B \U1F596 \U1F44C \u270C \U1F91E
        \U1F918 \U1F919 \U1F449 \U1F44A \U1F44D \U1F44E \u270A \U1F44A \U1F91B \U1F91C
        \U1F44F \U1F64C \U1F450 \U1F64F \u270D \U1F485 \U1F4AA
        \U1F442 \U1F443 \U1F444 \U1F445 \U1F441 \U1F48B
    } \
    People {
        \U1F476 \U1F466 \U1F467 \U1F471 \U1F468 \U1F469
        \U1F474 \U1F475 \U1F64D \U1F64E \U1F645 \U1F646 \U1F481 \U1F64B \U1F647
        \U1F926 \U1F937 \U1F46E \U1F482 \U1F477 \U1F934 \U1F478 \U1F473
        \U1F472 \U1F935 \U1F470 \U1F47C \U1F385
        \U1F486 \U1F487 \U1F6B6 \U1F3C3 \U1F483 \U1F57A \U1F46F
        \U1F46D \U1F46B \U1F46C \U1F48F \U1F491 \U1F46A
    } \
    Animals {
        \U1F435 \U1F412 \U1F436 \U1F415 \U1F429 \U1F43A
        \U1F431 \U1F408 \U1F42F \U1F405 \U1F406 \U1F434 \U1F40E
        \U1F42E \U1F402 \U1F403 \U1F404 \U1F437 \U1F416 \U1F417 \U1F43D
        \U1F40F \U1F411 \U1F410 \U1F43A \U1F42A \U1F42B \U1F418
        \U1F419 \U1F41C \U1F41D \U1F41E \U1F41F \U1F578 \U1F43E \U1F438
        \U1F43C \U1F427 \U1F426 \U1F427 \U1F54A
        \U1F40A \U1F422 \U1F40D
        \U1F432 \U1F409 \U1F433 \U1F40B \U1F42C \U1F41F \U1F420
        \U1F421 \U1F419 \U1F41A \U1F40C \U1F41C \U1F41D \U1F41E
    } \
    Food {
        \U1F347 \U1F348 \U1F349 \U1F34A \U1F34B \U1F34C \U1F34D \U1F34E \U1F34F
        \U1F350 \U1F351 \U1F352 \U1F353 \U1F345 \U1F346
        \U1F33D \U1F336 \U1F344 \U1F330 \U1F35E
        \U1F356 \U1F357 \U1F353
        \U1F354 \U1F35F \U1F355 \U1F32D \U1F32E \U1F32F
        \U1F35D \U1F35C \U1F372 \U1F365 \U1F363 \U1F364 \U1F359 \U1F35A \U1F358
        \U1F362 \U1F361 \U1F367 \U1F368 \U1F366 \U1F370 \U1F382 \U1F36E
        \U1F36D \U1F36C \U1F36B \U1F37F \U1F369 \U1F36A \U1F36F \U1F37C \u2615
        \U1F375 \U1F376 \U1F37E \U1F377 \U1F378 \U1F379 \U1F37A \U1F37B \U1F942
        \U1F943
    } \
    Travel {
        \U1F697 \U1F695 \U1F699 \U1F68C \U1F68E \U1F693 \U1F692 \U1F691 \U1F690
        \U1F69A \U1F69B \U1F69C \U1F6B2
        \U1F6A8 \U1F694 \U1F68D \U1F68B \U1F688 \U1F682 \U1F683 \U1F684 \U1F685
        \U1F686 \U1F687 \U1F689 \u2708 \U1F4BA
        \U1F680 \U1F6A4 \u26F5
        \U1F6A2 \u2693 \u26FD \U1F6A7 \U1F6A6 \U1F6A5 \U1F68F
        \U1F3F0 \U1F3EF \U1F3DF \U1F3A1 \U1F3A2 \U1F3A0 \u26F2
        \U1F3D6 \U1F30B \u26F0 \U1F5FB \U1F3D5 \U1F3E0 \U1F3E1 \U1F3D8
        \U1F3DA \U1F3D7 \U1F3ED \U1F3E2 \U1F3EC \U1F3E3 \U1F3E4 \U1F3E5 \U1F3E6 \U1F3E8
        \U1F3EA \U1F3EB \U1F3E9 \U1F492
    } \
    Activities {
        \u26BD \U1F3C0 \U1F3C8 \u26BE \U1F3BE \U1F3D0 \U1F3C9
        \U1F3B1 \U1F3D3 \U1F3F8 \U1F3D1 \U1F3CF \u26F3
        \U1F3A3 \U1F3BD \u26F8
        \U1F3BF \u26F7 \U1F3C2 \U1F3CB \u26F9 \U1F3CC \U1F3C4 \U1F3CA \U1F6A3 \U1F6B5 \U1F6B4
        \U1F3C6 \U1F3C5 \U1F396 \U1F3F5 \U1F397 \U1F39F \U1F3AB
        \U1F3AD \U1F3A8 \U1F3AC \U1F3A4 \U1F3A7 \U1F3BC \U1F3B9 \U1F941
        \U1F3B7 \U1F3BA \U1F3B8 \U1F3BB \U1F3B2 \u265F \U1F3AF \U1F3B3 \U1F3AE
        \U1F3B0
    } \
    Objects {
        \u231A \U1F4F1 \U1F4BB \u2328 \U1F5A5 \U1F5A8 \U1F5B1 \U1F5B2 \U1F579
        \U1F4BD \U1F4BE \U1F4BF \U1F4C0 \U1F4FC \U1F4F7 \U1F4F8 \U1F4F9 \U1F3A5 \U1F4FD
        \U1F39E \U1F4DE \u260E \U1F4DF \U1F4E0 \U1F4FA \U1F4FB \U1F399 \U1F39A \U1F39B
        \u23F1 \u23F2 \u23F0 \U1F570 \u231B \u23F3 \U1F4E1 \U1F50B \U1F50C
        \U1F4A1 \U1F526 \U1F56F \U1F6E2 \U1F4B8 \U1F4B5 \U1F4B4 \U1F4B6
        \U1F4B7 \U1F4B0 \U1F4B3 \U1F48E \u2696 \U1F527 \U1F528 \u2692 \U1F6E0
        \u26CF \U1F529 \u2699 \u26D3 \U1F4A3 \U1F52A \U1F5E1 \u2694 \U1F6E1 \U1F6AC \u26B0
        \U1F52E \U1F52D \U1F52C \U1F573 \U1F6BD
    } \
    Symbols {
        \u2764 \U1F49B \U1F49A \U1F499 \U1F49C \U1F494
        \u2763 \U1F495 \U1F49E \U1F493 \U1F497 \U1F496 \U1F498 \U1F49D \U1F49F \u262E
        \u271D \u262A \u2626 \u271F \u262F \u262A \u2626
        \U1F6D0 \u26CE \u2648 \u2649 \u264A \u264B \u264C \u264D \u264E \u264F
        \u2650 \u2651 \u2652 \u2653 \U1F194 \u269B \U1F251 \u2622 \u2623 \U1F4F4
        \U1F4F3 \U1F236 \U1F21A \U1F238 \U1F23A \U1F237 \u2734 \U1F19A \U1F4AE \U1F250
        \u3299 \u3297 \U1F234 \U1F235 \U1F239 \U1F232 \U1F170 \U1F171 \U1F18E \U1F191
        \U1F17E \U1F198 \u274C \u2B55 \U1F6D1 \u26D4 \U1F4DB \U1F6AB \U1F4AF \U1F4A2
        \u2668 \U1F6B7 \U1F6AF \U1F6B3 \U1F6B1 \U1F51E \U1F4F5 \U1F6AD \u2757 \u2755
        \u2753 \u2754 \u203C \u2049 \u303D \u26A0 \U1F6B8 \U1F531
        \u269C
    }]
}

# -- helpers

proc ::tk::emoji::IsSimple {e} {
    set n [string length $e]
    return [expr {$n == 1 || ($n == 2 && [string index $e 1] eq "\uFE0F")}]
}

proc ::tk::emoji::Items {cat} {
    variable data
    variable recent
    if {$cat eq "Recent"} {return $recent}
    set list [dict get $data $cat]
    set out {}
    set seen {}
    foreach e $list {
        if {![IsSimple $e]} continue
        if {[dict exists $seen $e]} continue
        dict set seen $e 1
        lappend out $e
    }
    return $out
}

# The picker is for X11 and Wayland; other windowing systems have their own.
proc ::tk::emoji::Active {} {
    return [expr {[tk windowingsystem] ni {win32 aqua}}]
}

proc ::tk::emoji::Supported {w} {
    if {![winfo exists $w]} {return 0}
    set c [winfo class $w]
    return [expr {$c in {Entry TEntry Text}}]
}

proc ::tk::emoji::Insert {w s} {
    if {![winfo exists $w]} return
    # Strip VS16 so we get text (monochrome) presentation rather than
    # letting a color font claim the glyph.
    set s [string map {"\uFE0F" ""} $s]
    switch -- [winfo class $w] {
        Entry  { ::tk::EntryInsert $w $s }
        TEntry { ttk::entry::Insert $w $s }
        Text   { ::tk::TextInsert $w $s }
    }
}

proc ::tk::emoji::EnsureFonts {} {
    variable S
    if {[info exists S(font)]} return
    # Monochrome fonts, in order of preference
    set candidates {
        {Noto Emoji}
        {Noto Sans Symbols2}
        {Noto Sans Symbols}
        {Symbola}
        {DejaVu Sans}
    }
    set family [font actual TkDefaultFont -family]
    set available [font families]
    foreach fam $candidates {
        if {$fam in $available} {
            set family $fam
            break
        }
    }
    set S(font)    [list $family 14]
    set S(catfont) [list $family 9]
    set S(bigfont) [list $family 14]
}

# -- public entry

proc ::tk::emoji::choose {{target ""}} {
    variable S
    if {![Active]} {return}
    if {$target eq ""} {set target [focus]}
    if {$target eq "" || ![winfo exists $target]} {bell; return}
    if {[winfo exists $S(top)] && [string match $S(top)* $target]} {return}
    if {![Supported $target]} {bell; return}
    set S(target) $target
    if {![winfo exists $S(top)]} {Build}
    Place
}

# -- building UI

proc ::tk::emoji::Build {} {
    variable S
    variable categories
    variable recent

    EnsureFonts
    set top $S(top)
    set S(drawnW) -1
    set S(cur) -1
    set S(items) {}
    set S(cat) [expr {[llength $recent] ? "Recent" : "Smileys"}]

    toplevel $top -class TkEmojiPicker
    wm withdraw $top
    wm title $top "Emoji"
    wm transient $top [winfo toplevel $S(target)]
    wm overrideredirect $top 0
    wm protocol $top WM_DELETE_WINDOW [namespace code Close]
    bind $top <Escape> [namespace code Close]

    # Minimal colors - monochrome friendly, royal blue selection
    set bg white
    set fg black
    set selbg "#4169E1"
    set selfg white
    set S(bg) $bg; set S(fg) $fg; set S(selbg) $selbg; set S(selfg) $selfg

    ttk::style configure Emoji.Toolbutton -font $S(catfont) -padding 1

    set f [ttk::frame $top.f -padding 2]
    pack $f -fill both -expand 1

	variable icons
    ttk::frame $f.bar
    foreach cat $categories {
        set key [string tolower $cat]
        set ico [expr {[dict exists $icons $cat] ? [dict get $icons $cat] : ""}]
        ttk::radiobutton $f.bar.$key -style Emoji.Toolbutton \
            -text "$ico $cat" -width 8 \
            -variable ::tk::emoji::S(cat) -value $cat \
            -command [namespace code SetCategory]
        pack $f.bar.$key -side left -padx 0
    }

    ttk::frame $f.f
    canvas $f.f.c -width [expr {12*$S(cell)}] -height [expr {6*$S(cell)}] \
        -highlightthickness 0 -borderwidth 0 -background $bg -takefocus 1 \
        -yscrollincrement $S(cell)
    ttk::scrollbar $f.f.sb -command [list $f.f.c yview]
    $f.f.c configure -yscrollcommand [list $f.f.sb set]
    pack $f.f.sb -side right -fill y
    pack $f.f.c -side left -fill both -expand 1
    set S(canvas) $f.f.c

    ttk::frame $f.pv
    ttk::label $f.pv.big -font $S(bigfont) -width 2 -anchor center
    ttk::label $f.pv.name -textvariable ::tk::emoji::S(cat) -anchor w -font $S(catfont)
    pack $f.pv.big -side left
    pack $f.pv.name -side left -padx 4
    set S(prevBig) $f.pv.big

    grid $f.bar -sticky w -pady 1
    grid $f.f   -sticky nsew -pady 1
    grid $f.pv  -sticky ew
    grid columnconfigure $f 0 -weight 1
    grid rowconfigure $f 1 -weight 1

    set c $S(canvas)
    bind $c <Configure>           [namespace code {Reflow %w}]
    bind $c <Motion>              [namespace code {Hover %x %y}]
    bind $c <ButtonPress-1>       [namespace code {Click %x %y 0}]
    bind $c <Shift-ButtonPress-1> [namespace code {Click %x %y 1}]
    bind $c <MouseWheel>          [namespace code {Wheel %D}]
    bind $c <Button-4>            [namespace code {Wheel 1}]
    bind $c <Button-5>            [namespace code {Wheel -1}]
    bind $c <Key-Left>            [namespace code {Move left}]
    bind $c <Key-Right>           [namespace code {Move right}]
    bind $c <Key-Up>              [namespace code {Move up}]
    bind $c <Key-Down>            [namespace code {Move down}]
    bind $c <Key-Home>            [namespace code {Move home}]
    bind $c <Key-End>             [namespace code {Move end}]
    bind $c <Return>              [namespace code {PickCurrent 0}]
    bind $c <space>               [namespace code {PickCurrent 0}]
    bind $c <Shift-Return>        [namespace code {PickCurrent 1}]
}

proc ::tk::emoji::Place {} {
    variable S
    set top $S(top)
    set t $S(target)
    update idletasks
    set w [winfo reqwidth $top]
    set h [winfo reqheight $top]
    set x [winfo rootx $t]
    set y [expr {[winfo rooty $t] + [winfo height $t]}]
    catch {
        lassign [$t bbox insert] bx by bw bh
        set x [expr {[winfo rootx $t] + $bx}]
        set y [expr {[winfo rooty $t] + $by + $bh + 2}]
    }
    set sw [winfo screenwidth $t]
    set sh [winfo screenheight $t]
    if {$x+$w > $sw} {set x [expr {$sw-$w}]}
    if {$y+$h > $sh} {set y [expr {[winfo rooty $t]-$h-2}]}
    wm geometry $top +[expr {max(0,$x)}]+[expr {max(0,$y)}]
    wm deiconify $top
    raise $top
    focus -force $S(canvas)
}

proc ::tk::emoji::Close {} {
    variable S
    set t $S(target)
    destroy $S(top)
    if {[winfo exists $t]} {catch {focus -force $t}}
}

# -- draw / nav

proc ::tk::emoji::SetCategory {} { Draw 0 }

proc ::tk::emoji::Reflow {width} {
    variable S
    if {$width != $S(drawnW)} {Draw 1}
}

proc ::tk::emoji::Origin {idx} {
    variable S
    set col [expr {$idx % $S(cols)}]
    set row [expr {$idx / $S(cols)}]
    return [list [expr {$S(x0)+$col*$S(cell)}] [expr {$row*$S(cell)}]]
}

proc ::tk::emoji::Draw {keep} {
    variable S
    set c $S(canvas)
    set cell $S(cell)
    set oldcur $S(cur)
    set frac [lindex [$c yview] 0]
    $c delete all
    set S(cur) -1
    set S(items) [Items $S(cat)]
    set S(drawnW) [winfo width $c]
    set width $S(drawnW)
    if {$width < $cell} {set width [$c cget -width]}
    set S(cols) [expr {max(1,$width/$cell)}]
    set S(x0) [expr {($width - $S(cols)*$cell)/2}]
    $c create rectangle 0 0 0 0 -fill $S(selbg) -outline {} -state hidden -tags cursor
    set i 0
    foreach e $S(items) {
        lassign [Origin $i] x y
        $c create text [expr {$x+$cell/2}] [expr {$y+$cell/2}] \
            -text $e -font $S(font) -fill $S(fg) -tags [list emoji e$i]
        incr i
    }
    if {$i==0} {
        $c create text [expr {$width/2}] [expr {$cell}] -fill gray50 \
            -text "Nothing yet" -font TkDefaultFont
    }
    $c lower cursor
    set rows [expr {($i+$S(cols)-1)/$S(cols)}]
    set S(total) [expr {max(1,$rows)*$cell}]
    $c configure -scrollregion [list 0 0 $width $S(total)]
    if {$keep} {
        $c yview moveto $frac
        if {$oldcur>=0 && $oldcur<$i} {SetCur $oldcur}
    } else {
        $c yview moveto 0
        $S(prevBig) configure -text ""
    }
}

proc ::tk::emoji::SetCur {idx} {
    variable S
    set c $S(canvas)
    if {$idx==$S(cur)} return
    if {$S(cur)>=0} {$c itemconfigure e$S(cur) -fill $S(fg)}
    set S(cur) $idx
    if {$idx<0} {
        $c itemconfigure cursor -state hidden
        $S(prevBig) configure -text ""
        return
    }
    set cell $S(cell)
    lassign [Origin $idx] x y
    $c coords cursor [expr {$x+1}] [expr {$y+1}] [expr {$x+$cell-1}] [expr {$y+$cell-1}]
    $c itemconfigure cursor -state normal
    $c itemconfigure e$idx -fill $S(selfg)
    $S(prevBig) configure -text [lindex $S(items) $idx]
}

proc ::tk::emoji::EnsureVisible {idx} {
    variable S
    set c $S(canvas); set h [winfo height $c]
    if {$S(total)<=$h} return
    set y [lindex [Origin $idx] 1]
    set top [$c canvasy 0]
    if {$y<$top} {
        $c yview moveto [expr {double($y)/$S(total)}]
    } elseif {$y+$S(cell) > $top+$h} {
        $c yview moveto [expr {double($y+$S(cell)-$h)/$S(total)}]
    }
}

proc ::tk::emoji::IndexAt {x y} {
    variable S
    set c $S(canvas)
    set cx [expr {[$c canvasx $x]-$S(x0)}]
    set cy [$c canvasy $y]
    if {$cx<0 || $cy<0} {return -1}
    set col [expr {int($cx)/$S(cell)}]
    set row [expr {int($cy)/$S(cell)}]
    if {$col>=$S(cols)} {return -1}
    set idx [expr {$row*$S(cols)+$col}]
    if {$idx >= [llength $S(items)]} {return -1}
    return $idx
}

proc ::tk::emoji::Hover {x y} {SetCur [IndexAt $x $y]}
proc ::tk::emoji::Wheel {d} {variable S; $S(canvas) yview scroll [expr {$d>0 ? -1 : 1}] units}
proc ::tk::emoji::Move {how} {
    variable S; set n [llength $S(items)]; if {$n==0} return
    set i $S(cur)
    switch -- $how {
        home {set i 0}
        end  {set i [expr {$n-1}]}
        default {
            if {$i<0} {set i 0} else {
                switch -- $how {
                    left  {incr i -1}
                    right {incr i}
                    up    {incr i [expr {-$S(cols)}]}
                    down  {incr i $S(cols)}
                }
            }
        }
    }
    set i [expr {max(0,min($n-1,$i))}]
    SetCur $i; EnsureVisible $i
}

proc ::tk::emoji::Click {x y keep} {
    set i [IndexAt $x $y]; if {$i>=0} {Pick $i $keep}
}
proc ::tk::emoji::PickCurrent {keep} {
    variable S; if {$S(cur)>=0} {Pick $S(cur) $keep}
}
proc ::tk::emoji::Pick {idx keep} {
    variable S; variable recent; variable maxRecent
    set e [lindex $S(items) $idx]
    if {![winfo exists $S(target)]} {Close; return}
    Insert $S(target) $e
    set recent [linsert [lsearch -all -inline -not -exact $recent $e] 0 $e]
    set recent [lrange $recent 0 [expr {$maxRecent-1}]]
    if {!$keep} {Close}
}

# -- bindings

if {[::tk::emoji::Active]} {
    event add <<EmojiPicker>> <Control-period>
    event add <<EmojiPicker>> <Control-KP_Decimal>
    event add <<EmojiPicker>> <Control-semicolon>

    # Super (Mod4) variants. A compositor may reserve these for itself, in
    # which case Tk never sees them.
    event add <<EmojiPicker>> <Mod4-period>
    event add <<EmojiPicker>> <Mod4-semicolon>

    # Class bindings with break, so the key is handled here and only once.
    foreach cls {Entry TEntry Text} {
        bind $cls <<EmojiPicker>> {::tk::emoji::choose %W; break}
    }
}

# demo when run directly
if {[info exists ::argv0] && [info script] ne "" \
        && [file normalize $::argv0] eq [file normalize [info script]]} {
    wm title . "Emoji picker demo"
    ttk::frame .f -padding 10; pack .f -fill both -expand 1
    ttk::label .f.l1 -text "Entry (Ctrl-. or Super-.) :"
    ttk::entry .f.e -width 40
    ttk::label .f.l2 -text "Text (Ctrl-. or Super-.) :"
    text .f.t -width 40 -height 6
    pack .f.l1 -anchor w; pack .f.e -fill x -pady {2 8}
    pack .f.l2 -anchor w; pack .f.t -fill both -expand 1
    focus .f.e
}
