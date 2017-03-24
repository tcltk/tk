# Android location/GPS/NMEA demo

package require Tcl 8.5
package require Tk
package require Borg
catch {package require vu}

proc make_items c {
    set mw [winfo screenmmwidth $c]
    set mh [winfo screenmmheight $c]
    if {$mw < $mh} {
	set mh $mw
    }
    set fs [expr round($mh / 6)]
    set fonts(ttbig) [list {DejaVu Sans Mono} $fs]
    set fs [expr round($mh / 12)]
    set fonts(normal) [list {DejaVu Sans} $fs]
    set fonts(tt) [list {DejaVu Sans Mono} $fs]
    $c create text 10 20 -tags pos_NS -anchor nw -font $fonts(ttbig)
    $c create text 10 70 -tags pos_EW -anchor nw -font $fonts(ttbig)
    $c create text 10 120 -tags pos_A -anchor nw -font $fonts(ttbig)
    $c create text 10 330 -tags nmea -anchor nw -fill gray75 -font $fonts(tt)
    $c create text 600 70 -tags rate -anchor nw -fill black \
	-font $fonts(ttbig) -text " stop "
    $c create rectangle {*}[$c bbox rate] -tags rateb -width 5 \
	-outline orange -fill orange
    $c lower rateb
    for {set i 0} {$i < 10} {incr i} {
	set x [expr {60 + $i * 70}]
	catch {
	    $c create sticker $x 200 [expr {$x + 50}] 300 \
		-tags snr_$i -fill {} -orient vertical \
		-font $fonts(normal) \
		-relheight 0 -relwidth 1 -relx 0 -rely 0 \
		-space 0 -width 0 -bar blue -color white \
		-outline white
	}
    }
    .c bind rate  <1> [list change_rate $c]
    .c bind rateb <1> [list change_rate $c]
}

proc change_rate c {
    set text [$c itemcget rate -text]
    switch -glob -- $text {
	{ 60 *} {
	    $c itemconfigure rate -text " stop "
	    set rate 0
	}
	{ 30 *} {
	    $c itemconfigure rate -text " 60 s "
	    set rate 60000
	}
	{ 15 *} {
	    $c itemconfigure rate -text " 30 s "
	    set rate 30000
	}
	{ 10 *} {
	    $c itemconfigure rate -text " 15 s "
	    set rate 15000
	}
	{  5 *} {
	    $c itemconfigure rate -text " 10 s "
	    set rate 15000
	}
	default {
	    $c itemconfigure rate -text "  5 s "
	    set rate 10000
	}
    }
    if {$rate <= 0} {
	borg location stop
    } else {
	borg location start $rate
    }
}

proc location_update c {
    set t [borg location get]
    borg log verbose AndroWish "location: $t"
    array set pos [borg location get]
    if {[info exists pos(gps)]} {
	array set pos $pos(gps)
	set color green
    } elseif {[info exists pos(network)]} {
	array set pos $pos(network)
	set color green2
    }
    if {[info exists pos(latitude)]} {
	set ns [format "%12.6f" [expr abs($pos(latitude))]]
	if {$pos(latitude) < 0} {
	    append ns "\u00b0 S"
	} else {
	    append ns "\u00b0 N"
	}
	set ew [format "%12.6f" [expr abs($pos(longitude))]]
	if {$pos(longitude) < 0} {
	    append ew "\u00b0 W"
	} else {
	    append ew "\u00b0 E"
	}
	set alt [format "%7.1f MAMSL" $pos(altitude)]
	$c itemconfigure pos_NS -fill $color -text $ns
	$c itemconfigure pos_EW -fill $color -text $ew
	$c itemconfigure pos_A -fill $color -text $alt
    } else {
	$c itemconfigure pos_NS -fill yellow
	$c itemconfigure pos_EW -fill yellow
	$c itemconfigure pos_A -fill yellow
    }
}

proc gps_update c {
    set color blue
    set t [borg location gps]
    borg log verbose AndroWish "gps: $t"
    array set state $t
    if {$state(state) eq "off"} {
	set color blue4
	$c itemconfigure pos_NS -fill yellow
	$c itemconfigure pos_EW -fill yellow
	$c itemconfigure pos_A -fill yellow
	for {set i 0} {$i < 10} {incr i} {
	    $c itemconfigure snr_$i -bar $color
	}
    }
    set t [borg location satellites]
    if {$t eq ""} {
	for {set i 0} {$i < 10} {incr i} {
	    $c itemconfigure snr_$i -bar blue4
	}
	return
    }
    borg log verbose AndroWish "sat: $t"
    array set sat $t
    for {set i 0} {$i < 10} {incr i} {
	if {[info exists sat($i)]} {
	    set v [dict get $sat($i) snr]
	    set t [dict get $sat($i) prn]
	} else {
	    set v 0
	    set t ""
	}
	if {$v > 60} {
	    set v 60
	}
	set v [expr {$v / 60.0}]
	$c itemconfigure snr_$i -rely [expr {1 - $v}] -relheight $v \
	    -text $t -bar $color
    }
}

proc nmea_update c {
    array set nmea [borg location nmea]
    if {[info exists nmea(nmea)]} {
	set text [split [$c itemcget nmea -text] "\n"]
	set t [split [string map [list "\r" ""] [string trim $nmea(nmea)]]]
	lappend text {*}$t
	set text [lrange $text end-15 end]
	$c itemconfigure nmea -text [join $text "\n"]
    }
}

wm attributes . -fullscreen 1
borg screenorientation landscape

canvas .c -bg black -bd 0 -highlightthickness 0
pack .c -side top -fill both -expand 1 -padx 0 -pady 0

make_items .c

bind all <Key-Break> exit

bind . <<LocationUpdate>> [list location_update .c] 
bind . <<GPSUpdate>> [list gps_update .c] 
bind . <<NMEAUpdate>> [list nmea_update .c]

change_rate .c

