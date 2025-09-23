# attrib.tcl --
#
# This file defines the procedures ::tk::attrib::Table and
# ::tk::attrib::OnDestroy.  Both are invoked from the C code.
#
# Copyright © 2025 Csaba Nemethi <csaba.nemethi@t-online.de>

namespace eval ::tk::attrib {
    # Array variable mapping widget names to lists of attribute table names
    variable Tables
}

# ::tk::attrib::Table --
#   This procedure is invoked by the "tk attribtable" command.  It creates an
#   attribute table of a given name as a procedure in the namespace of the
#   calling context if not fully qualified.
#
# Arguments:
#   name	The name of the table.
#
# Results:
#   Returns nothing.

proc ::tk::attrib::Table tableName {
    set tableName2 [list $tableName]

    # Create the namespace $tableName2
    if {[namespace exists $tableName2]} {
	# Delete the namespace $tableName2.  This will unset all the
	# attributes of all widgets set using the $tableName proc.
	namespace delete $tableName2

	# Remove $tableName from all elements of the Tables array
	variable Tables
	foreach w [array names Tables] {
	    set lst $Tables($w)
	    if {[set idx [lsearch -exact $lst $tableName]] >= 0} {
		set Tables($w) [lremove $lst $idx]
	    }
	}
    }
    namespace eval $tableName2 {}

    # Create the procedure $tableName in the namespace of the calling context

    # $tableName --
    #	This procedure queries or modifies arbitrary attributes of a given
    #	widget.  It supports the following forms:
    #
    #	$tableName set pathName name value ?name value ...?
    #	    Sets the specified attributes to the given values.  Returns an
    #	    empty string.
    #
    #	$tableName get pathName ?name ?defaultValue??
    #	    If a name is specified then returns the corresponding attribute
    #	    value, or an empty string or $defaultValue (if given) if no
    #	    corresponding value exists.  Otherwise returns a list consisting of
    #	    all attributenames and values, sorted by the names.
    #
    #	$tableName names pathName
    #	    Returns a sorted list consisting of all attribute names.
    #
    #	$tableName unset pathName name ?name ...?
    #	    Unsets the specified attributes.  Returns an empty string.
    #
    #	$tableName clear pathName
    #	    Unsets all attributes and removes $pathName from the list of those
    #	    widgets that have attributes set via $tableName set.  Returns an
    #	    empty string.
    #
    #	$tableName exists pathName name
    #	    Returns 1 if the attribute of the given name exists and 0
    #	    otherwise.
    #
    #	$tableName pathnames
    #	    Returns a list consisting of the path names of all widgets that
    #	    have attributes set via $tableName set.
    #
    # Arguments:
    #	op	One of set, get, names, unset, clear, exists, or pathnames.
    #	args	Additional arguments, mostly starting with a widget path name.
    #
    # Results:
    #	See the description above.

    if {[catch {
	uplevel 1 [list proc $tableName {op args} [format {
	    set table {%s}
	    set table2 [list $table]
	    set ns ::tk::attrib::$table2

	    if {$op ni {set get names unset clear exists pathnames}} {
		return -code error "usage: $table2\
			set|get|names|unset|clear|exists pathName ..."
	    }

	    set argCount [llength $args]
	    if {$argCount > 0} {
		set w [lindex $args 0]
		if {$op ne "exists" && ![winfo exists $w]} {
		    return -code error "bad window path name [list $w]"
		}

		set args [lrange $args 1 end]
		incr argCount -1
	    }

	    switch $op {
		set {
		    if {$argCount == 0 || $argCount %% 2 != 0} {
			return -code error "usage: $table2 set [list $w]\
				name value ?name value ...?"
		    }

		    # Set the specified attributes to the given values
		    if {![array exists ${ns}::$w]} {
			namespace eval $ns [list variable $w]
		    }
		    upvar ${ns}::$w attribs
		    foreach {name val} $args {
			set attribs($name) $val
		    }

		    # Update or create the array element of
		    # the name $w in ::tk::attrib::Tables
		    upvar ::tk::attrib::Tables arr
		    if {[info exists arr($w)]} {
			set lst $arr($w)
			if {$table ni $lst} {
			    lappend lst $table
			}
		    } else {
			set lst [list $table]
		    }
		    set arr($w) $lst

		    return ""
		}
		get {
		    if {$argCount == 1 || $argCount == 2} {
			# Return the value of the specified attribute
			set defaultVal [expr {$argCount == 2 ?
					      [lindex $args 1] : ""}]
			if {[array exists ${ns}::$w]} {
			    upvar ${ns}::$w attribs
			    set name [lindex $args 0]
			    return [expr {[info exists attribs($name)] ?
					  $attribs($name) : $defaultVal}]
			} else {
			    return $defaultVal
			}
		    } elseif {$argCount == 0} {
			# Return the list of all attribute names and values
			if {[array exists ${ns}::$w]} {
			    upvar ${ns}::$w attribs
			    set result {}
			    foreach name [lsort [array names attribs]] {
				lappend result $name $attribs($name)
			    }
			    return $result
			} else {
			    return [list]
			}
		    } else {
			return -code error "usage: $table2 get [list $w]\
				?name ?defaultValue??"
		    }
		}
		names {
		    if {$argCount != 0} {
			return -code error "usage: $table2 names [list $w]"
		    }

		    return [lsort [array names ${ns}::$w]]
		}
		unset {
		    if {$argCount == 0} {
			return -code error "usage: $table2 unset [list $w]\
				name ?name ...?"
		    }

		    if {[array exists ${ns}::$w]} {
			# Unset the specified attributes
			upvar ${ns}::$w attribs
			foreach name $args {
			    if {[info exists attribs($name)]} {
				unset attribs($name)
			    }
			}
		    }
		    return ""
		}
		clear {
		    if {$argCount != 0} {
			return -code error "usage: $table2 clear [list $w]"
		    }

		    # Unset the array ${ns}::$w if it exists
		    array unset ${ns}::$w
		    return ""
		}
		exists {
		    if {$argCount != 1} {
			return -code error "usage: $table2 exists [list $w]\
				name"
		    }

		    # Return 1 if the given attribute exists and 0 otherwise
		    if {[array exists ${ns}::$w]} {
			upvar ${ns}::$w attribs
			set name [lindex $args 0]
			return [info exists attribs($name)]
		    } else {
			return 0
		    }
		}
		pathnames {
		    set lst {}
		    foreach arr [info vars ${ns}::*] {
			if {[array size $arr] != 0} {
			    lappend lst [namespace tail $arr]
			}
		    }
		    return $lst
		}
	    }
	} $tableName]]
    } result] != 0} {
	return -code error $result
    }
}

# ::tk::attrib::OnDestroy --
#   This procedure is invoked automatically by the function Tk_DestroyWindow
#   when a given widget is destroyed.  It unsets all attributes of the widget,
#   set by all attribute table procs.
#
# Arguments:
#   w	The name of the widget that is being destroyed.
#
# Results:
#   Returns nothing.

proc ::tk::attrib::OnDestroy w {
    upvar ::tk::attrib::Tables arr
    if {[info exists arr($w)]} {
	foreach table $arr($w) {
	    $table clear $w
	}
	unset arr($w)
    }
}
