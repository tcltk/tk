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
			    error $msg $msg [list TK LOOKUP OPTION $option]
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
		    my UpdateState $args
		    return
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
	    proc DefineProperty {name options} {
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

		uplevel 1 [list \
		    method <OptDescribe-$name> {} $descriptor]
		uplevel 1 [list \
		    forward <OptValidate-$name> {*}$validator]
		uplevel 1 [list \
		    ::oo::configuresupport::readableproperties -append -$name]
		if {![dict get $options init]} {
		    uplevel 1 [list \
			::oo::configuresupport::writableproperties -append -$name]
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
		uplevel 1 [list \
		    forward <OptDescribe-$name> my <OptDescribe$otherName>]
		uplevel 1 [list \
		    forward <OptValidate-$name> my <OptValidate$otherName>]
		uplevel 1 [list \
		    ::oo::configuresupport::readableproperties -append -$name]
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
		uplevel 1 [list Support::DefineProperty $name $Opt]
	    } on error {msg opt} {
		dict unset opt -errorinfo
		return -options $opt $msg
	    }
	}
	namespace path ::oo::define
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

    ::oo::abstract create propertytype {
	private variable def

	constructor {default} {
	    set def $default
	}

	method validate {value} {
	    error "UNIMPLEMENTED" UNIMPLEMENTED
	}

	method Normalize {value} {
	    return $value
	}

	method default {} {
	    return $def
	}

	self {
	    unexport new

	    method createbool {name default testCommand} {
		::set name [::namespace tail $name]
		::set fullname [::namespace current]::$name
		::set fullname [::tk::BoolTestType create \
				  $fullname $default $testCommand]
		::set map [::namespace ensemble configure ::tk::PropType -map]
		::dict set map $name [::list $fullname]
		::namespace ensemble configure ::tk::PropType -map $map
		::return $fullname
	    }

	    method createthrow {name default testCommand} {
		::set name [::namespace tail $name]
		::set fullname [::namespace current]::$name
		::set fullname [::tk::ThrowTestType create \
				  $fullname $default $testCommand]
		::set map [::namespace ensemble configure ::tk::PropType -map]
		::dict set map $name [::list $fullname]
		::namespace ensemble configure ::tk::PropType -map $map
		::return $fullname
	    }

	    method createtable {name default table} {
		::set name [::namespace tail $name]
		::set fullname [::namespace current]::$name
		::set fullname [::tk::TableType create $fullname $default $table]
		::set map [::namespace ensemble configure ::tk::PropType -map]
		::dict set map $name [::list $fullname]
		::namespace ensemble configure ::tk::PropType -map $map
		::return $fullname
	    }
	}
    }

    ::oo::class create BoolTestType {
	superclass propertytype
	constructor {default test} {
	    next $default
	    oo::objdefine [self] forward Validate {*}$test
	}

	method validate {value} {
	    if {![my Validate $value]} {
		set type [namespace tail [self]]
		return -code error "invalid $type value \"$value\""
	    }
	    try {
		return [my Normalize $value]
	    } on error msg {
		return -code error $msg
	    }
	}
    }

    ::oo::class create ThrowTestType {
	superclass propertytype
	constructor {default test} {
	    next $default
	    oo::objdefine [self] method Validate value $test
	}

	method validate {value} {
	    try {
		my Validate $value
		return [my Normalize $value]
	    } on error msg {
		return -code error $msg
	    }
	}
    }

    ::oo::class create TableType {
	superclass propertytype
	private variable Table
	constructor {default table} {
	    next $default
	    set Table $table
	}
	method validate {value} {
	    tcl::prefix match $Table $value
	}
    }

    # Install the types: those with a boolean test
    propertytype createbool boolean "false" {
	string is boolean -strict
    }
    propertytype createbool zboolean "" {
	string is boolean
    }
    propertytype createbool integer "0" {
	string is entier -strict
    }
    propertytype createbool zinteger "" {
	string is entier
    }
    propertytype createbool float "0.0" {
	string is double -strict
    }
    propertytype createbool zfloat "" {
	string is double
    }
    propertytype createbool list "" {
	string is list
    }
    propertytype createbool dict "" {
	string is dict
    }

    # Install the types: those with an erroring test
    oo::objdefine [propertytype createthrow string {} {}] {
	# Special case; everything valid
	method validate value {return $value}
    }
    propertytype createthrow distance "0px" {
	winfo fpixels . $value
    }
    propertytype createthrow image "" {
	if {$value ne ""} {
	    image type $value
	}
    }
    propertytype createthrow color "black" {
	winfo rgb . $value
    }
    propertytype createthrow zcolor "" {
	if {$value ne ""} {
	    winfo rgb . $value
	}
    }
    propertytype createthrow font "TkDefaultFont" {
	# Cheapest property to read
	font metrics $value -fixed
    }

    # Install the types: those with an element table
    propertytype createtable anchor "center" {
	n ne e se e sw w nw center
    }
    propertytype createtable justify "left" {
	center left right
    }
    propertytype createtable relief "flat" {
	flat groove raised ridge solid sunken
    }
}

# Local Variables:
# mode: tcl
# c-basic-offset: 4
# fill-column: 78
# End:
