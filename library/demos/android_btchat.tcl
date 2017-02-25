# Example: chat over Bluetooth

set uuid "edc4e9ef-9c92-4293-9f56-d7154928ead5"

proc tput {text} {
    .text configure -state normal
    .text insert 1.0 $text {} "\n"
    .text configure -state disabled
    .text yview 1.0
}

proc tclear {} {
    .text configure -state normal
    .text delete 1.0 end
    .text configure -state disabled
}

proc connect {} {
    if {[info exists ::txsock]} {
	txclose "closing connection"
	return
    }
    set devs [borg bluetooth devices]
    set cdev [.top.btlist get]
    set index 0
    foreach {addr name} $devs {
	if {$name eq $cdev} {
	    set found $addr
	    break
	}
	incr index
    }
    set ndevs {}
    foreach {dummy name} $devs {
	lappend ndevs $name
    }
    .top.btlist configure -values [lsort -ascii $ndevs]
    if {[info exists found]} {
	.top.btlist set $cdev
	if {!$::spp} {
	    lappend found $::uuid
	}
	if {[catch {rfcomm -async $found 0} sock]} {
	    tput $sock
	} else {
	    tput "connecting to [lindex $found 0]"
	    .top.conn configure -text Disconnect
	    fconfigure $sock -blocking 0 -buffering line
	    fileevent $sock readable [list receive $sock]
	    set ::txsock $sock
	}
    }
}

proc receive {sock} {
    if {[catch {gets $sock line} ret]} {
	txclose $ret
	return
    }
    if {$ret < 0} {
	if {[fblocked $sock]} {
	    return
	}
	txclose "closed connection"
	return
    }
    tput "<< $line"
}

proc send {} {
    set line [.send get]
    if {[info exists ::txsock]} {
	if {[catch {puts $::txsock $line}] ||
	    [catch {flush $::txsock}]} {
	    txclose "write error, closing connection"
	} else {
	    tput ">> $line"
	    .send delete 0 end
	}
    }
}

proc accept {sock args} {
    txclose "closing connection"
    tput "connect from $args"
    fconfigure $sock -blocking 0 -buffering line
    fileevent $sock readable [list receive $sock]
    set ::txsock $sock
    .top.conn configure -text Disconnect
}

proc txclose {text} {
    if {[info exists ::txsock]} {
	catch {close $::txsock}
	unset ::txsock
	.top.conn configure -text Connect
	tput $text
    }
}

proc btstat {} {
    if {[borg bluetooth state] eq "on"} {
	if {![info exists ::srvsock]} {
	    set myaddr [list $::uuid BT-chat]
	    if {[catch {rfcomm -server accept -myaddr $myaddr 0} ::srvsock]} {
		tput "server socket failed: $::srvsock"
		unset ::srvsock
	    }
	}
    } else {
	catch {close $::srvsock}
	catch {unset ::srvsock}
	txclose "Bluetooth disabled, closing connection"
    }
}

wm attributes . -fullscreen 1
bind all <Key-Break> exit
bind . <<Bluetooth>> btstat
frame .top
::ttk::button .top.conn -text Connect -width 12 -command connect
::ttk::combobox .top.btlist -state readonly
set spp 0
::ttk::checkbutton .top.spp -text SPP -variable spp
pack .top -side top -fill x
pack .top.conn -side left -padx 10 -pady 10
pack .top.btlist -side left -fill x -expand 1 -padx 10 -pady 10
pack .top.spp -side left -padx 10 -pady 10

::ttk::entry .send
pack .send -side top -fill x -padx 10 -pady 10
bind .send <Return> send

text .text -state disabled
pack .text -side top -fill both -expand 1 -padx 10 -pady 10
bind .text <Double-1> tclear

after idle btstat
after idle connect
