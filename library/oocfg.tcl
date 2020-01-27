# oocfg.tcl --
#
# 	This file contains a configure system for megawidgets written in TclOO.
#
# Copyright (c) 2020 Donal K. Fellows
#
# See the file "license.terms" for information on usage and redistribution of
# this file, and for a DISCLAIMER OF ALL WARRANTIES.

# 8.7, or any 9.*
package require Tcl 8.7-9.99

namespace eval ::tk {
    namespace ensemble create -command ::tk::PropType

    ::oo::class create configurable {
	variable ""

	private {
	    method GetRealOptionName {option} {
		set props [info object properties [self] -all -readable]
		try {
		    return [::tcl::prefix match -message "option" \
				 $props $option]
		} on error msg {
		    return -code error -errorcode \
			[list TK LOOKUP OPTION $option] $msg
		}
	    }

	    method AllDescriptors {} {
		lmap opt [info object properties [self] -all -readable] {
		    list $opt {*}[my <OptDescribe$opt>] $($opt)
		}
	    }

	    method OneDescriptor {option} {
		set opt [my GetRealOptionName $option]
		list $opt {*}[my <OptDescribe$opt>] $($opt)
	    }

	    method UpdateState opts {
		set props [info object properties [self] -all -writable]
		set rprops [info object properties [self] -all -readable]
		set stateChanged 0
		set state [array get ""]
		try {
		    foreach {option value} $opts {
			try {
			    set opt [::tcl::prefix match -message "option" \
					 $props $option]
			} on error {msg} {
			    try {
				::tcl::prefix match $rprops $option
			    } on error {} {
				# Do nothing
			    } on ok {optionName} {
				set msg "read only option: $optionName"
			    }
			    return -code error -errorcode \
				[list TK LOOKUP OPTION $option] $msg
			}
			set ($opt) [my <OptValidate$opt> $value]
			set stateChanged 1
		    }
		    if {$stateChanged} {
			my PostConfigure
		    }
		} on error {msg opt} {
		    # Rollback on error
		    array set "" $state
		    return -options $opt $msg
		}
	    }
	}

	method configure args {
	    try {
		if {[llength $args] == 0} {
		    return [my AllDescriptors]
		} elseif {[llength $args] == 1} {
		    return [my OneDescriptor [lindex $args 0]]
		} elseif {[llength $args] % 2 == 0} {
		    return [my UpdateState $args]
		}
	    } on error {msg opt} {
		# Hide the implementation details
		dict unset opt -errorinfo
		return -options $opt $msg
	    }
	    # Don't like the genuine Tk errors; they're weird!
	    return -code error -errorcode {TCL WRONGARGS} \
		[format {wrong # args: should be "%s"} \
		     "[self] configure ?-option value ...?"]
	}

	method cget {option} {
	    try {
		return $([my GetRealOptionName $option])
	    } on error {msg opt} {
		# Hide the implementation details
		dict unset opt -errorinfo
		return -options $opt $msg
	    }
	}

	method PostConfigure {} {}

	method Initialise {pathName args} {
	    if {[llength $args] % 1} {
		# TODO: generate a better error
		return -code error "wrong # args"
	    }
	    set toSet {}
	    set props [info object properties [self] -all -readable]
	    try {
		foreach opt $props {
		    lassign [my <OptDescribe$opt>] nm cls def
		    set val [option get $pathName $nm $cls]
		    if {$val eq ""} {
			set val $def
		    }
		    dict set toSet $opt $val
		}
		foreach {option value} $args {
		    try {
			set opt [::tcl::prefix match -message "option" \
				     $props $option]
		    } on error msg {
			return -code error -errorcode \
			    [list TK LOOKUP OPTION $option] $msg
		    }
		    dict set toSet $opt [my <OptValidate$opt> $value]
		}
	    } on error {msg opt} {
		dict unset opt -errorinfo
		return -options $opt $msg
	    }
	    array set "" $toSet
	}
	forward Initialize my Initialise
    }

    namespace eval PropertyDefine {
	namespace eval Support {
	    proc DefineProperty {contextClass name options} {
		set descriptor list
		lappend descriptor [dict get $options name]
		lappend descriptor [dict get $options class]
		set type [dict get $options type]
		if {[dict exists $options def]} {
		    lappend descriptor [dict get $options def]
		} else {
		    lappend descriptor [::tk::PropType $type default]
		}
		set validator [list ::tk::PropType $type validate]

		oo::define $contextClass method <OptDescribe-$name> $descriptor
		oo::define $contextClass forward <OptValidate-$name> \
		    {*}$validator
		oo::define $contextClass \
		    ::oo::configuresupport::readableproperties -append -$name
		if {![dict get $options init]} {
		    oo::define $contextClass \
			::oo::configuresupport::writableproperties -append -$name		    
		}
	    }

	    proc DefineAlias {contextClass name otherName} {
		set targets [info class methods $contextClass -private -all]
		if {"<OptDescribe$otherName>" ni $targets} {
		    error "no such option \"$otherName\""
		}
		if {"<OptValidate$otherName>" ni $targets} {
		    error "no such option \"$otherName\""
		}
		oo::define $contextClass forward <OptDescribe-$name> \
		    my <OptDescribe$otherName>
		oo::define $contextClass forward <OptValidate-$name> \
		    my <OptValidate$otherName>
		oo::define $contextClass \
		    ::oo::configuresupport::readableproperties -append -$name
	    }
	}

	namespace export property

	proc property {name args} {
	    set contextClass [uplevel 1 self]
	    if {[llength $args] % 2} {
		return -code error -errorcode {TCL WRONGARGS} \
			[format {wrong # args: should be "%s"} \
			    "property name ?-option value ...?"]
	    }
	    if {![regexp -nocase {^[[:alpha:]]\w*$} $name]} {
		return -code error "bad property name \"$name\":\
			must be alphanumeric starting with a letter"
	    }
	    dict set Opt type string
	    dict set Opt class [string totitle $name]
	    dict set Opt name [string tolower $name]
	    dict set Opt init false
	    set defFromType [::tk::PropType string default]
	    foreach {option value} $args {
		set option [tcl::prefix match {
		    -alias -class -default -initonly -name -type
		} $option]
		if {$option eq "-alias"} {
		    if {[llength $args] != 2} {
			return -code error \
			    "-alias may only ever be used on its own"
		    }
		    try {
			uplevel 1 [list \
				Support::DefineAlias $contextClass $name $value]
		    } on error {msg opt} {
			dict unset opt -errorinfo
			return -options $opt $msg
		    }
		}
		switch $option {
		    -class {
			dict set Opt class $value
			# TODO: warn or error if first char not uppercase?
		    }
		    -name {
			dict set Opt name $value
			# TODO: warn or error if first char not lowercase?
		    }
		    -default {
			dict set Opt def $value
		    }
		    -initonly {
			if {![string is boolean -strict $value]} {
			    return -code error "bad boolean \"$value\""
			}
			dict set Opt init $value
		    }
		    -type {
			dict set Opt type $value
			set defFromType [::tk::PropType $value default]
		    }
		}
	    }
	    if {![dict exists $Opt def]} {
		dict set Opt def $defFromType
	    }
	    try {
		uplevel 1 [list \
			Support::DefineProperty $contextClass $name $Opt]
	    } on error {msg opt} {
		dict unset opt -errorinfo
		return -options $opt $msg
	    }
	}
	namespace path :oo::define
    }

    ::oo::define configurable {
	definitionnamespace -class ::tk::PropertyDefine
	# Individual widgets do not support defining their own properties
    }
    ::oo::class create megawidget {
	superclass ::oo::class

    	constructor {{definitionScript ""}} {
	    next {mixin ::tk::configurable}
	    next $definitionScript
	}
	definitionnamespace -class ::tk::PropertyDefine
    }

    ::oo::class create propertytype {
	private variable test def

	constructor {testCommand default} {
	    set test $testCommand
	    set def $default
	}

	method validate {value} {
	    try {
		if {![{*}$test $value]} {
		    set type [namespace tail [self]]
		    return -code error "invalid $type value \"$value\""
		}
		return [my Normalize $value]
	    } on error msg {
		return -code error $msg
	    }
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

    # Install the types: those with a boolean test
    propertytype create integer {string is entier -strict} "0"
    propertytype create zinteger {string is entier} ""
    propertytype create float {string is double -strict} "0.0"
    propertytype create zfloat {string is double} ""
    propertytype create list {string is list} ""
    propertytype create dict {string is dict} ""

    # Install the types: those with an erroring test
    oo::objdefine [propertytype create string {} {}] {
	# Special case; everything valid
	method validate value {return $value}
    }
    oo::objdefine [propertytype create distance {my Validate} "0px"] {
	method Validate value {
	    winfo fpixels . $value
	    return 1
	}
    }
    oo::objdefine [propertytype create image {my Validate} ""] {
	method Validate value {
	    if {$value ne ""} {image type $value}
	    return 1
	}
    }
    oo::objdefine [propertytype create color {my Validate} "black"] {
	method Validate value {
	    winfo rgb . $value
	    return 1
	}
    }
    oo::objdefine [propertytype create zcolor {my Validate} ""] {
	method Validate value {
	    if {$value ne ""} {winfo rgb . $value}
	    return 1
	}
    }
    oo::objdefine [propertytype create font {my Validate} "TkDefaultFont"] {
	method Validate value {
	    # Cheapest property to read
	    font metrics $value -fixed
	    return 1
	}
    }

    # Install the types: those with an element table
    propertytype createtable anchor {n ne e se e sw w nw center} "center"
    propertytype createtable justify {center left right} "left"
    propertytype createtable relief {flat groove raised ridge solid sunken} "flat"
}

# Local Variables:
# mode: tcl
# c-basic-offset: 4
# fill-column: 78
# End:
