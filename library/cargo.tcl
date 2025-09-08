# cargo.tcl --
#
# This file defines the procedure tk_cargo, which queries
# or modifies arbitrary attributes of a given widget.
#
# Copyright © 2025 Csaba Nemethi <csaba.nemethi@t-online.de>

# Create the ::tk::cargo namespace if needed
namespace eval ::tk::cargo {}

# ::tk_cargo --
#   This procedure queries or modifies arbitrary attributes of a given widget.
#   It supports the following forms:
#
#   tk_cargo set $w name value ?name value ...?
#	Sets the specified attribute(s) to the given value(s).  Returns an
#	empty string.
#
#   tk_cargo get $w ?name?
#	If a name is specified then returns the corresponding attribute value,
#      	or an empty string if no corresponding value exists.  Otherwise returns
#	a list consisting of all attribute names and values.
#
#   tk_cargo unset $w ?name ...?
#	Unsets the specified attribute(s), or all attributes if no names are
#	given.  Returns an empty string.
#
#   tk_cargo exists $w name
#	Returns 1 if the attribute of the given name exists and 0 otherwise.
#
#   REMARK: When the widget $w is destroyed, all of its attributes are
#           automatically unset (by the function Tk_DestroyWindow).
#
# Arguments:
#   op		One of set, get, unset, or exists.
#   w		The widget whose attributes are queried or modified.
#   args	Attribute names or name-value pairs.
#
# Results:
#   See the description above.

proc ::tk_cargo {op w args} {
    if {$op ni {set get unset exists}} {
	return -code error "usage: tk_cargo set|get|unset|exists pathName ..."
    }
    if {![winfo exists $w]} {
	return -code error "bad window path name \"$w\""
    }

    set argCount [llength $args]
    switch $op {
	set {
	    if {$argCount == 0 || $argCount % 2 != 0} {
		return -code error \
			"usage: tk_cargo set $w name value ?name value ...?"
	    }

	    # Set the specified attributes to the given values
	    if {![array exists ::tk::cargo::$w]} {
		namespace eval ::tk::cargo [list variable $w]
	    }
	    upvar ::tk::cargo::$w attribs
	    foreach {name val} $args {
		set attribs($name) $val
	    }
	    return ""
	}
	get {
	    if {$argCount == 1} {
		# Return the value of the specified attribute
		if {[array exists ::tk::cargo::$w]} {
		    upvar ::tk::cargo::$w attribs
		    set name [lindex $args 0]
		    return [expr {[info exists attribs($name)] ?
				  $attribs($name) : ""}]
		} else {
		    return ""
		}
	    } elseif {$argCount == 0} {
		# Return the list of all attribute names and values
		if {[array exists ::tk::cargo::$w]} {
		    upvar ::tk::cargo::$w attribs
		    set result {}
		    foreach name [lsort [array names attribs]] {
			lappend result $name $attribs($name)
		    }
		    return $result
		} else {
		    return [list]
		}
	    } else {
		return -code error "usage: tk_cargo get $w ?name?"
	    }
	}
	unset {
	    if {$argCount == 0} {
		# Unset the array ::tk::cargo::$w if it exists
		array unset ::tk::cargo::$w
	    } elseif {[array exists ::tk::cargo::$w]} {
		# Unset the specified attributes
		upvar ::tk::cargo::$w attribs
		foreach name $args {
		    if {[info exists attribs($name)]} {
			unset attribs($name)
		    }
		}
	    }
	    return ""
	}
	exists {
	    if {$argCount != 1} {
		return -code error "usage: tk_cargo exists $w name"
	    }

	    # Return 1 if the specified attribute exists and 0 otherwise
	    if {[array exists ::tk::cargo::$w]} {
		upvar ::tk::cargo::$w attribs
		set name [lindex $args 0]
		return [info exists attribs($name)]
	    } else {
		return 0
	    }
	}
    }
}
