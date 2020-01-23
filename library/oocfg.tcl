# oocfg.tcl --
#
# 	This file contains a configure system for megawidgets written in TclOO.
#
# Copyright (c) 2020 Donal K. Fellows
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.

# TODO: Find out how to say 8.7 and 9.0
package require Tcl 8.7

namespace eval ::tk {
    namespace ensemble create -command ::tk::PropType

    ::oo::class create configurable {
	variable ""

	private {
	    method AllDescriptors {} {}
	    method OneDescriptor option {}
	    method UpdateState opts {}
	}

	method configure args {
	    if {[llength $args] == 0} {
		return [my AllDescriptors]
	    } elseif {[llength $args] == 1} {
		return [my OneDescriptor [lindex $args 0]]
	    } elseif {[llength $args] % 2 == 0} {
		return [my UpdateState $args]
	    } else {
		# Don't like the genuine Tk errors; they're weird!
		return -code error -errorcode {TCL WRONGARGS} \
			[format {wrong # args: should be "%s"} \
			    "[self] configure ?-option value ...?"]
	    }
	}

	method cget option {
	    set props [info object properties [self] -all -readable]
	    try {
		set opt [::tk::prefix match -message "option" $props $option]
	    } on error {msg} {
		return -code error -errorcode [list TK LOOKUP OPTION $option] \
		    $msg
	    }
	    return [my <ReadOpt$opt>]
	}

	method PostConfigure {} {}
	method Initialise {pathName args} {}
	forward Initialize my Initialise
    }

    namespace eval PropertySupport {
	proc property args {
	}
    }

    ::oo::class create megawidget {
	superclass ::oo::class
    }

    ::oo::class create propertytype {
	private variable test def

	constructor {testCommand default} {
	    set test $testCommand
	    set def $default
	}

	method validate {value} {
	    if {![{*}$test $value]} {
		set type [namespace tail [self]]
		return -code error "invalid $type value \"$value\""
	    }
	    return [my Normalize $value]
	}

	method Normalize {value} {
	    return $value
	}

	method default {} {
	    return $def
	}

	self {
	    unexport new

	    method create {name testCommand default} {
		set name [namespace tail $name]
		set fullname [namespace current]::$name
		set fullname [next $fullname $testCommand $default]
		set map [namespace ensemble configure ::tk::PropType -map]
		dict set map $name [list $fullname]
		namespace ensemble configure ::tk::PropType -map $map
		return $fullname
	    }

	    method createtable {name table default} {
		set name [namespace tail $name]
		set fullname [namespace current]::$name
		set fullname [::tk::TableType create $fullname $table $default]
		set map [namespace ensemble configure ::tk::PropType -map]
		dict set map $name [list $fullname]
		namespace ensemble configure ::tk::PropType -map $map
		return $fullname
	    }
	}
    }

    ::oo::class create TableType {
	superclass propertytype
	private variable Table
	constructor {table default} {
	    next {} $default
	    set Table $table
	}
	method validate {value} {
	    tcl::prefix match $Table $value
	}
    }

    propertytype create string {apply {x {return 1}}} {}
    propertytype create integer {string is entier -strict} 0
    propertytype create zinteger {string is entier} {}
    propertytype create float {string is double -strict} 0.0
    propertytype create zfloat {string is double} {}
    propertytype createtable justify {center left right} left
}

# Local Variables:
# mode: tcl
# c-basic-offset: 4
# fill-column: 78
# End:
