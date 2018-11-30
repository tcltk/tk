# path.tcl --
#
#       Various support procedures for the path widget.
#
#  Copyright (c) 2018 r.zaumseil@freenet.de
#

namespace eval ::path {
    # All functions inside this namespace return a transormation matrix.
    namespace eval matrix {
	namespace export *
	namespace ensemble create
    }
    # All functions inside this namespace return a path description.
    namespace eval path {
	namespace export *
	namespace ensemble create
    }
}

# ::path::matrix::rotate --
# Arguments:
#   angle   Angle in grad
#   cx      X-center coordinate
#   cy      Y-center coordinate
# Results:
#       The transformation matrix.
proc ::path::matrix::rotate {angle {cx 0} {cy 0}} {
    set myCos [expr {cos($angle)}]
    set mySin [expr {sin($angle)}]
    if {$cx == 0 && $cy == 0} {
        return [list $myCos $mySin [expr {-1.*$mySin}] $myCos 0 0]
    }
    return [list $myCos $mySin [expr {-1.*$mySin}] $myCos \
        [expr {$cx - $myCos*$cx + $mySin*$cy}] \
        [expr {$cy - $mySin*$cx - $myCos*$cy}]]
}

# ::path::matrix::scale --
# Arguments:
#   sx  Scaling factor x-coordinate
#   sy  Scaling factor y-coordinate
# Results:
#       The transformation matrix.
proc ::path::matrix::scale {sx {sy {}}} {
    if {$sy eq {}} {set sy $sx}
    return [list $sx 0 0 $sy 0 0]
}

# ::path::matrix::flip --
# Arguments:
#   fx  1 no flip, -1 horizontal flip
#   fy  1 no flip, -1 vertical flip
# Results:
#       The transformation matrix.
proc ::path::matrix::flip {{cx 0} {cy 0} {fx 1} {fy 1}} {
    return [list $fx 0 0 $fy [expr {$cx*(1-$fx)}] [expr {$cy*($fy-1)}]]
}

# ::path::matrix::rotateflip --
# Arguments:
#   angle   Angle in grad
#   cx      X-center coordinate
#   cy      Y-center coordinate
#   fx      1 no flip, -1 horizontal flip
#   fy      1 no flip, -1 vertical flip
# Results:
#       The transformation matrix.
proc ::path::matrix::rotateflip {{angle 0} {cx 0} {cy 0} {fx 1} {fy 1}} {
    set myCos [expr {cos($angle)}]
    set mySin [expr {sin($angle)}]
    if {$cx == 0 && $cy == 0} {
    return [list [expr {$fx*$myCos}] [expr {$fx*$mySin}] \
        [expr {-1.*$mySin*$fy}] [expr {$myCos*$fy}] 0 0]
    }
    return [list [expr {$fx*$myCos}] [expr {$fx*$mySin}] \
        [expr {-1.*$mySin*$fy}] [expr {$myCos*$fy}] \
        [expr {$myCos*$cx*(1.-$fx) - $mySin*$cy*($fy-1.) + $cx - $myCos*$cx + $mySin*$cy}] \
        [expr {$mySin*$cx*(1.-$fx) + $myCos*$cy*($fy-1.) + $cy - $mySin*$cx - $myCos*$cy}] \
    ]

}

# ::path::matrixs::kewx --
# Arguments:
#   angle   Angle in grad
# Results:
#       The transformation matrix.
proc ::path::matrix::skewx {angle} {
    return [list 1 0 [expr {tan($angle)}] 1 0 0]
}

# ::path::matrix::skewy --
# Arguments:
#   angle   Angle in grad
# Results:
#       The transformation matrix.
proc ::path::matrix::skewy {angle} {
    return [list 1 [expr {tan($angle)}] 0 1 0 0]
}

# ::path::matrix::move --
# Arguments:
#   dx  Difference in x direction
#   dy  Difference in y direction
# Results:
#       The transformation matrix.
proc ::path::matrix::move {dx dy} {
    return [list 1 0 0 1 $dx $dy]
}

# ::path::matrix::mult --
# Arguments:
#   ma  First matrix
#   mb  Second matrix
# Results:
#       Product of transformation matrices.
proc ::path::matrix::mult {ma mb} {
    lassign $ma a1 b1 c1 d1 x1 y1
    lassign $mb a2 b2 c2 d2 x2 y2
    return [list \
        [expr {$a1*$a2 + $c1*$b2}] [expr {$b1*$a2 + $d1*$b2}] \
        [expr {$a1*$c2 + $c1*$d2}] [expr {$b1*$c2 + $d1*$d2}] \
        [expr {$a1*$x2 + $c1*$y2 + $x1}] [expr {$b1*$x2 + $d1*$y2 + $y1}]]
}

# ::path::path::ellipse --
# Arguments:
#   x   Start x coordinate
#   y   Start y coordinate
#   rx  Radius in x direction
#   ry  Radius in y direction
# Results:
#   The path definition.
proc ::path::path::ellipse {x y rx ry} {
    return [list M $x $y a $rx $ry 0 1 1 0 [expr {2*$ry}] a $rx $ry 0 1 1 0 [expr {-2*$ry}] Z]
}

# ::path::path::circle --
# Arguments:
#   x   Start x coordinate
#   y   Start y coordinate
#   r   Radius of circle
# Results:
#       The path definition.
proc ::path::path::circle {x y r} {
    return [list M $x $y a $r $r 0 1 1 0 [expr {2*$r}] a $r $r 0 1 1 0 [expr {-2*$r}] Z]
}

# ::path::gradientstopsstyle --
#       Utility function to create named example gradient definitions.
# Arguments:
#       name      the name of the gradient
#       args
# Results:
#       The stops list.
proc ::path::gradientstopsstyle {name args} {
    switch -- $name {
    rainbow {
        return {
            {0.00 "#ff0000"}
            {0.15 "#ff7f00"}
            {0.30 "#ffff00"}
            {0.45 "#00ff00"}
            {0.65 "#0000ff"}
            {0.90 "#7f00ff"}
            {1.00 "#7f007f"}
        }
    }
    default {
        return -code error "the named gradient '$name' is unknown"
    }
    }
}

# vim: set ts=4 sw=4 sts=4 ff=unix et :
