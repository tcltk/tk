# Android demo for accelerometer, app life cycle, finger events

proc ademo {} {
    global ademo
    # pan/zoom plus translated finger events
    catch {sdltk touchtranslate 12}
    wm protocol . WM_DELETE_WINDOW ademo_done
    wm attribute . -fullscreen 1

    labelframe .am -text "Accelerometer" -padx 5 -pady 5
    pack .am -side top -padx 5 -pady 5 -anchor nw
    set row 0
    foreach axis {1 2 3} {
        label .am.a$axis -text $axis
        label .am.v$axis -textvariable ademo(a$axis) -width 8 \
	    -relief sunken -bg #FFFFFF
	grid .am.a$axis -row $row -column 0
	grid .am.v$axis -row $row -column 1
	incr row
    }
    set ademo(accel) 0
    frame .am.b
    grid .am.b -row $row -column 0 -columnspan 2 -padx 3
    radiobutton .am.b.on -text On -width 5 -variable ademo(accel) \
	-value 1 -command {sdltk accelerometer $ademo(accel)} \
	-indicatoron 0 -selectcolor [.am.b cget -bg]
    radiobutton .am.b.off -text Off -width 5 -variable ademo(accel) \
	-value 0 -command {sdltk accelerometer $ademo(accel)} \
	-indicatoron 0 -selectcolor [.am.b cget -bg]
    pack .am.b.on .am.b.off -side left -padx 3 -pady 3 -expand 1
    bind . <<Accelerometer>> {set ademo(a%s) %x}

    labelframe .fi -text "Finger Events" -padx 5 -pady 5
    pack .fi -side top -padx 5 -pady 5 -anchor nw
    set row 0
    label .fi.lx -text X
    label .fi.ly -text Y
    label .fi.ldx -text DX
    label .fi.ldy -text DY
    label .fi.lp -text P
    grid .fi.lx -row $row -column 1
    grid .fi.ly -row $row -column 2
    grid .fi.ldx -row $row -column 3
    grid .fi.ldy -row $row -column 4
    grid .fi.lp -row $row -column 5
    incr row
    foreach f {1 2 3 4 5} {
        label .fi.n$f -text $f
        label .fi.x$f -textvariable ademo(fx$f) -width 8 \
	    -relief sunken -bg #FFFFFF
        label .fi.y$f -textvariable ademo(fy$f) -width 8 \
	    -relief sunken -bg #FFFFFF
        label .fi.dx$f -textvariable ademo(fdx$f) -width 8 \
	    -relief sunken -bg #FFFFFF
        label .fi.dy$f -textvariable ademo(fdy$f) -width 8 \
	    -relief sunken -bg #FFFFFF
        label .fi.p$f -textvariable ademo(fp$f) -width 8 \
	    -relief sunken -bg #FFFFFF
	grid .fi.n$f -row $row -column 0
	grid .fi.x$f -row $row -column 1
	grid .fi.y$f -row $row -column 2
	grid .fi.dx$f -row $row -column 3
	grid .fi.dy$f -row $row -column 4
	grid .fi.p$f -row $row -column 5
	incr row
    }
    bind . <<FingerDown>> {ademo_finger down %s %x %y %X %Y %t}
    bind . <<FingerUp>> {ademo_finger up %s %x %y %X %Y %t}
    bind . <<FingerMotion>> {ademo_finger motion %s %x %y %X %Y %t}

    labelframe .lc -text "App Life Cycle, Viewport, etc." -padx 5 -pady 5
    pack .lc -side top -padx 5 -pady 5 -anchor nw
    text .lc.t -width 35 -height 6 -bg #FFFFFF -state disabled
    pack .lc.t -side top
    foreach ev {LowMemory Terminating WillEnterBackground
	DidEnterBackground WillEnterForeground DidEnterForeground} {
	bind . <<$ev>> [list ademo_lcevt $ev]
    }
    bind . <<ViewportUpdate>> {ademo_vpt %x %y %X %Y %s}

    bind . <<JoystickAdded>> {ademo_jaddrem JoystickAdd %X}
    bind . <<JoystickRemoved>> {ademo_jaddrem JoystickRemove %X}
    bind . <<JoystickMotion>> {ademo_jmotion %X %s %x}
    bind . <<TrackballMotion>> {ademo_tmotion %X %s %x %y}
    bind . <<HatPosition>> {ademo_hatpos %X %s %x}
    bind . <<JoystickButtonUp>> {ademo_jbut JoystickButtonUp %X %s}
    bind . <<JoystickButtonDown>> {ademo_jbut JoystickButtonDown %X %s}

    frame .b -padx 5 -pady 5
    pack .b -side top -padx 5 -pady 5 -anchor nw
    button .b.x -text "Exit" -command ademo_done
    button .b.c -text "Console ..." -command {
	console hide ; console show
    }
    pack .b.x .b.c -side left -padx 5
}

proc ademo_lcevt {name} {
   .lc.t config -state normal
   .lc.t insert end $name
   .lc.t insert end "\n"
   .lc.t yview end
   .lc.t config -state disabled
}

proc ademo_vpt {x y w h s} {
   .lc.t config -state normal
   .lc.t insert end "Viewport $x,$y,$w,$h,$s"
   .lc.t insert end "\n"
   .lc.t yview end
   .lc.t config -state disabled
}

proc ademo_jaddrem {ev dev} {
   .lc.t config -state normal
   .lc.t insert end "$ev $dev"
   .lc.t insert end "\n"
   .lc.t yview end
   .lc.t config -state disabled
}

proc ademo_jmotion {dev s x} {
   .lc.t config -state normal
   .lc.t insert end "JoystickMotion $dev,$s,$x"
   .lc.t insert end "\n"
   .lc.t yview end
   .lc.t config -state disabled
}

proc ademo_tmotion {dev s x y} {
   .lc.t config -state normal
   .lc.t insert end "TrackballMotion $dev,$s,$x,$y"
   .lc.t insert end "\n"
   .lc.t yview end
   .lc.t config -state disabled
}

proc ademo_hatpos {dev s x} {
   .lc.t config -state normal
   .lc.t insert end "HatPosition $dev,$s,$x"
   .lc.t insert end "\n"
   .lc.t yview end
   .lc.t config -state disabled
}

proc ademo_jbut {ev dev s} {
   .lc.t config -state normal
   .lc.t insert end "$ev $dev,$s"
   .lc.t insert end "\n"
   .lc.t yview end
   .lc.t config -state disabled
}

proc ademo_finger {op id x y dx dy p} {
    global ademo
    if {$id < 1 || $id > 6} {
	return
    }
    set ademo(fx$id) $x
    set ademo(fy$id) $y
    set ademo(fdx$id) $dx
    set ademo(fdy$id) $dy
    set ademo(fp$id) $p
}

proc ademo_done {} {
    global ademo
    sdltk accelerometer 0
    unset ademo
    exit 0
}

ademo
