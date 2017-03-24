# Android demo for <<Sensor>> events

package require Borg

proc mksensor {data} {
    array set sensor $data
    set i $sensor(index)
    set f .s$i
    labelframe $f -text $sensor(name)
    ttk::checkbutton $f.en -text "Enable" -command [list togglesensor $i] \
	-variable ::SENSOR($i,enabled)
    grid $f.en -row 0 -column 0 -sticky w -padx 5 -pady 5
    entry $f.val -textvariable ::SENSOR($i,values) -width 35 \
	-disabledforeground black -disabledbackground white \
	-state disabled
    grid $f.val -row 0 -column 1 -sticky ew -padx 5 -pady 5
    pack $f -side top -padx 10 -pady 10
    set ::SENSOR($i,enabled) 1
    updatesensor $i
}

proc updatesensor {i} {
    array set sensor [borg sensor get $i]
    set ::SENSOR($i,values) $sensor(values)
    if {$::SENSOR($i,enabled)} {
	set ::SENSOR($i,enabled) $sensor(enabled)
    }
}

proc togglesensor {i} {
    if {$::SENSOR($i,enabled)} {
	borg sensor enable $i
    } else {
	borg sensor disable $i
    }
}

proc watchdog {} {
    after cancel watchdog
    after 10000 watchdog
    foreach s [lrange [borg sensor list] 0 5] {
	array set data $s
	set i $data(index)
	array set sensor [borg sensor get $i]
	if {!$sensor(enabled)} {
	    set ::SENSOR($i,enabled) 0
	}
    }
}

wm attributes . -fullscreen 1
bind . <Break> exit
bind . <<SensorUpdate>> {updatesensor %x}
label .top -text "Device Sensors"
pack .top -side top -pady 10
foreach s [lrange [borg sensor list] 0 5] {
    mksensor $s
}
watchdog
