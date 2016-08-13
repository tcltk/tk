# Example usage of the touch interface

if 0 {
    The current interface is as follows.

    All configuration is done via:
    wm touch <win> ?options?

    Some gestures are on by default. It seems like all but rotate.
    To get raw touch events, they must be turned on.

    To control gestures:
    -all : Turn on all gestures
    -pressandtap <bool> : Turn on or off PressAndTap gesture
    -rotate <bool> : Turn on or off Rotate gesture
    -twofingertap <bool> : Turn on or off TwoFingerTap gesture
    -zoom <bool> : Turn on or off Zoom gesture
    -pan <bool> : Turn on or off Pan gestures
    -pansfh <bool> : Turn on or off Pan single finger horizontal gesture
    -pansfv <bool> : Turn on or off Pan single finger vertical gesture
    -pangutter <bool> : Turn on or off Pan gutter mode
    -paninertia <bool> : Turn on or off Pan inertial mode
    See GESTURECONFIG in win API for pan flags.

    To get raw touch events:
    If any of the flags -touch, -fine or -wantpalm is given,
    the window is registered to receive raw touch event.
    It will no longer receive any gesture events.
    The flags -fine/-wantpalm corresponds to the Windows API
    RegisterTouchWindow.

    Events are received as <Touch> events.
    These events support at least base %W %x %y %X %Y fields.
    All extra information is given in a dictionary in the %d field.

    Any boolean field in the dictionary is either present with
    the value 1 or not present. Below they are written without a value.

    Touch fields:
    event = touch : Identify event as a touch. (i.e. not a gesture)
    id <val> : Id to know what events belong to the same touch.
    flags <val> : Raw flags value, in case future flags are provided.
    down : Start of touch
    move : Touch is moving
    up   : End of touch
    primary : First touch in a multi touch. Primary touch might also cause
              button events.
    inrange/nocoalesce/palm : See TOUCHINPUT in Windows API.

    Gesture fields:
    event = gesture : Identify event as a gesture.
    flags <val> : Raw flags value, in case future flags are provided.
    begin : Start of gesture
    end : End of gesture
    inertia : Gesture has triggered inertia

    Note that begin and end can both be set, e.g. in two finger tap.

    gesture <type> : Where type is one of:
      zoom : Zoom gesture. %x/y is between fingers
      pan  : Pan gesture. %x/y is between fingers
      rotate : Rotate gesture. %x/y is between fingers
      twofingertap : Two finger tap gesture. %x/y is between fingers
      pressandtap : Press and tap gesture. %x/y is first finger
    
    distance <i> : For zoom/pan/twofingertap: Distance between fingers.
    angle <r> : For rotate: Rotation angle in radians.
    deltax <i> : For pressandtap: Locates second finger. Valid with begin.
    deltay <i> : For pressandtap: Locates second finger. Valid with begin.
    inertiax <i> : For pan: Inertia vector. Valid with inertia flag.
    inertiay <i> : For pan: Inertia vector. Valid with inertia flag.
}

namespace import tcl::mathop::*

proc rndCol {} {
    set lst {orange yellow green cyan blue purple violet pink}
    set i [expr {int(rand()*[llength $lst])}]
    return [lindex $lst $i]
}

proc Circle {w x y r args} {
    $w create oval [- $x $r] [- $y $r] [+ $x $r] [+ $y $r] \
	-fill "" -outline black -width 2 {*}$args
}

proc Touch1 {W d x y X Y} {
    set id [dict get $d id]

    set move [dict exists $d move]
    set down [dict exists $d down]
    set up   [dict exists $d up]
    set primary [dict exists $d primary]

    if {![info exists ::t($id,id)]} {
	set ::t($id,id) [$W create oval $x $y $x $y]
	if {$primary} {
	    $W itemconfigure $::t($id,id) -fill red
	} else {
	    $W itemconfigure $::t($id,id) -fill [rndCol]
	}
    }
    if {$up} {
	$W delete $::t($id,id)
	array unset ::t $id,*
	return
    }

    # Filter unnecessary movement
    if {$move && $x == $::t($id,x) && $y == $::t($id,y)} {
	return
    }
    set r [expr {[winfo screenwidth .] / 50}]
    set ::t($id,x) $x
    set ::t($id,y) $y
    $W coords $::t($id,id) [- $x $r] [- $y $r] [+ $x $r] [+ $y $r]
}

proc Log {W d} {
    # Make a little log of messages for now
    if {[lindex $::messages 9] ne $d} {
	lappend ::messages $d
	set ::messages [lrange $::messages end-9 end]
    }
    set txt [join $::messages \n]
    $W itemconfigure gesture -text $txt
    $W raise gesture
}

proc Touch2 {W d x y X Y} {
    if {[dict get $d event] ne "gesture"} return

    switch [dict get $d gesture] {
        twofingertap {
            $W delete twofingertap
            set r [expr {[dict get $d distance] / 2}]
            Circle $W $x $y $r -fill yellow -tags twofingertap
        }
        pressandtap {
            if {[dict exists $d begin]} {
                # Only the begin message has delta set
                $W delete pressandtap
                set x1 [expr {$x + [dict get $d deltax]}]
                set y1 [expr {$y + [dict get $d deltay]}]
                $W create line $x $y $x1 $y1 -width 5 -fill red \
                        -tags pressandtap
            }
            if {[dict exists $d end]} {
                # Make end visible by changing colour
                $W itemconfigure pressandtap -fill purple
            }
        }
        zoom {
            $W delete zoom
            set r [expr {[dict get $d distance] / 2}]
            Circle $W $x $y $r -fill blue -tags zoom
        }
        pan {
            $W delete pan
            set dist [dict get $d distance]
            if {$dist == 0} {
                # Must be one finger?
                set r 40
                set col red
            } else {
                set r [expr {$dist / 2}]
                set col green
            }
            Circle $W $x $y $r -fill $col -tags pan
        }
        rotate {
            $W delete rotate
            set a [expr {180.0*[dict get $d angle]/3.141592 - 20}]
            set r [expr {$::size/4}]
            $W create arc [- $x $r] [- $y $r] [+ $x $r] [+ $y $r] \
                    -fill orange -outline black -width 2 -tags rotate \
                    -start $a -extent 40
        }
    }
    Log $W $d
}

#console show
set ::size [expr {[winfo screenwidth .] / 4}]
canvas .c1 -width $::size -height $::size -bd 3 -relief solid
canvas .c2 -width $::size -height $::size -bd 3 -relief solid
canvas .c3 -width $::size -height $::size -bd 3 -relief solid
.c1 create text [expr {$::size /2}] [expr {$::size /2}] -text Touch
lappend ::messages "Gesture"
.c2 create text [expr {$::size / 2}] [expr {$::size / 2}] -text GestureAll -tags gesture
.c3 create text [expr {$::size / 2}] [expr {$::size / 2}] -text Gesture -tags gesture

grid .c1 -   -sticky news
grid .c2 .c3 -sticky news
grid columnconfigure . all -weight 1
grid rowconfigure . all -weight 1
wm touch .c1 -touch
wm touch .c2 -all
wm touch .c3 -pan 1 -pansfv 0 -pansfh 0 -pangutter 0 -paninertia 0
bind .c1 <Touch> "Touch1 %W %d %x %y %X %Y"
bind .c2 <Touch> "Touch2 %W %d %x %y %X %Y"
bind .c3 <Touch> "Touch2 %W %d %x %y %X %Y"
