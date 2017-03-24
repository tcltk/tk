# Android demo for camera: simple webcam over wifi

package require Borg
package require tkpath

set ::port 8080
sdltk screensaver 0
wm attributes . -fullscreen 1
. configure -bg "#202020"
bind all <Break> exit
bind . <Configure> camrotate

proc camrotate {} {
    borg camera orientation [dict get [borg displaymetrics] rotation]
    img blank
    img configure -width 1 -height 1
    img configure -width 0 -height 0
    .c coords img [expr {[winfo screenwidth .] / 2}] \
	[expr {[winfo screenheight .] / 2}]
    .c coords txt [expr {[winfo screenwidth .] / 2}] \
	[expr {[winfo screenheight .] - 70}]
}

proc request {sock args} {
    chan configure $sock -translation binary -blocking 0 -buffering none
    if {[bind . <<PictureTaken>>] ne ""} {
	chan close $sock
	return
    }
    after 100
    catch {chan read $sock 1000} err
    borg camera parameters rotation [dict get [borg displaymetrics] rotation]
    if {![borg camera takejpeg]} {
	chan close $sock
	borg camera parameters rotation 0
	borg camera start
	return
    }
    bind . <<PictureTaken>> [list send_jpeg $sock]
    chan configure $sock -blocking 1
    chan puts -nonewline $sock \
  "HTTP/1.0 200 OK\r\nConnection: close\r\nContent-Type: image/jpeg\r\n\r\n"
}

proc send_jpeg {sock} {
    bind . <<PictureTaken>> {}
    catch {chan puts -nonewline $sock [borg camera jpeg]}
    catch {chan close $sock}
    borg camera parameters rotation 0
    borg camera start
}

proc netstat {} {
    set url OFFLINE
    set col red
    if {[string match "wifi*" [borg networkinfo]]} {
	set wifi [borg systemproperties wifi.interface]
	if {![catch {set ip [borg systemproperties dhcp.${wifi}.ipaddress]}] &&
	    ($ip ne "")} {
	    set url "http://${ip}:${::port}/"
	    set col green
	    unset wifi
	}
    } else {
	array set t [borg tetherinfo]
	if {$t(active) ne ""} {
	    catch {set wifi [borg systemproperties wifi.tethering.interface]}
	    if {![info exists wifi]} {
		set wifi $t(active)
	    }
	}
    }
    if {[info exists wifi]} {
	catch {
	    set ip [exec ifconfig $wifi]
	    set i [lsearch $ip ip]
	    if {$i > 0} {
		set ip [lindex $ip $i+1]
		set url "http://${ip}:${::port}/"
		set col green
	    }
	}
    }
    .c itemconfigure txt -text $url -stroke $col
}

image create photo img
bind . <<ImageCapture>> {borg camera image img}
bind . <<NetworkInfo>> netstat
bind . <<TetherInfo>> netstat
pack [tkp::canvas .c -bd 0 -highlightthickness 0 -bg "#202020"] \
    -side top -fill both -expand 1
.c create image 0 0 -image img -anchor c -tags img
.c create ptext 0 0 -fontfamily Helvetica -fontsize 40 -fill white \
   -fillopacity 0.8 -stroke red -strokeopacity 0.8 -strokewidth 5 \
   -filloverstroke 1 -textanchor c -tags txt
socket -server request $::port
after idle netstat
after idle camrotate
borg camera open
borg camera parameters picture-size 640x480 jpeg-quality 80 rotation 0
borg camera start
