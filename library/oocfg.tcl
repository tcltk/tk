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

    # ----------------------------------------------------------------------
    #
    # tk::PropertyDefine --
    #
    #	The definition language namespace for configurable widget definitions.
    #
    # ----------------------------------------------------------------------

    namespace eval PropertyDefine {
	namespace eval Support {
	    namespace path {::tcl ::tk ::oo::configuresupport}

	    proc DefineProperty {name options} {
		set descriptor list
		lappend descriptor [dict get $options name]
		lappend descriptor [dict get $options class]
		set type [dict get $options type]
		if {[dict exists $options def]} {
		    lappend descriptor [dict get $options def]
		} else {
		    lappend descriptor [PropType $type default]
		}
		set validator [list [namespace which PropType] $type validate]

		uplevel 1 [list \
		    method <OptDescribe$name> {} $descriptor]
		uplevel 1 [list \
		    forward <OptValidate$name> {*}$validator]
		uplevel 1 [list \
		    [namespace which readableproperties] -append $name]
		if {![dict get $options init]} {
		    uplevel 1 [list \
			[namespace which writableproperties] -append $name]
		}
	    }

	    proc ListProperties {class {type -readable}} {
		tailcall info class properties $class -all $type
	    }

	    proc DefineAlias {contextClass name otherName} {
		if {![string index $otherName 0] eq "-"} {
		    set otherName "-$otherName"
		}
		set targets [info class methods $contextClass \
				 -scope unexported -all]
		if {$otherName ni [ListProperties $contextClass]} {
		    throw [list TK LOOKUP OPTION $otherName] \
			"no such option \"$otherName\""
		}
		if {$otherName ni [ListProperties $contextClass -writable]} {
		    throw [list TK LOOKUP OPTION $otherName] \
			"may not alias init-only option \"$otherName\""
		}
		uplevel 1 [list \
		    forward <OptDescribe$name> my <OptDescribe$otherName>]
		uplevel 1 [list \
		    forward <OptValidate$name> my <OptValidate$otherName>]
		uplevel 1 [list \
		    [namespace which readableproperties] -append $name]
		uplevel 1 [list \
		    [namespace which writableproperties] -append $name]
	    }

	    proc ParsePropertyOptions {name args} {
		dict set opt type string
		dict set opt class [string totitle $name]
		dict set opt name [string tolower $name]
		dict set opt init false
		set defFromType [PropType string default]
		foreach {option value} $args {
		    switch [prefix match {
			-alias -class -default -initonly -name -type
		    } $option] {
			-alias {
			    if {[llength $args] != 2} {
				throw {TK PROPERTY_MISUSE} \
				    "-alias may only ever be used on its own"
			    }
			    dict set opt alias $value
			}
			-class {
			    dict set opt class $value
			    # TODO: warn or error if first char not uppercase?
			}
			-name {
			    dict set opt name $value
			    # TODO: warn or error if first char not lowercase?
			}
			-default {
			    dict set opt def $value
			}
			-initonly {
			    if {![string is boolean -strict $value]} {
				# TODO: produce a better error?
				throw {TK PROPERTY_MISUSE} \
				    "bad boolean \"$value\""
			    }
			    dict set opt init $value
			}
			-type {
			    dict set opt type $value
			    set defFromType [PropType $value default]
			}
		    }
		}
		if {[dict exists $opt def]} {
		    # Apply the type validation to the default
		    dict set opt def [PropType [dict get $opt type] validate \
					  [dict get $opt def]]
		} else {
		    dict set opt def $defFromType
		}
		return $opt
	    }

	    proc property {name args} {
		set contextClass [uplevel 1 self]
		if {[llength $args] % 2} {
		    return -code error -errorcode {TCL WRONGARGS} \
			[format {wrong # args: should be "%s"} \
			     "property name ?-option value ...?"]
		}
		if {![regexp -nocase {^[[:alpha:]]\w*$} $name]} {
		    return -code error -errorcode {TK PROPERTY_NAME} \
			"bad property name \"$name\":\
		    	must be alphanumeric starting with a letter"
		}
		try {
		    set Opt [ParsePropertyOptions $name {*}$args]
		    if {[dict exists $Opt alias]} {
			uplevel 1 [list \
			    [namespace which DefineAlias] $contextClass -$name \
			    [dict get $Opt alias]]
		    } else {
			uplevel 1 [list \
			    [namespace which DefineProperty] -$name $Opt]
		    }
		} on error {msg opt} {
		    # Condition the errorinfo trace
		    dict unset opt -errorinfo
		    dict incr opt -level
		    return -options $opt $msg
		}
	    }

	    namespace export property
	}

	namespace import Support::property
	namespace export property
	namespace path ::oo::define
    }

    # ----------------------------------------------------------------------
    #
    # tk::configurable --
    #
    #	The (mixin) class for configurable widgets.
    #
    #	Tricky point: namespace path of classes is uncertain; fully qualify
    #	everything.
    #
    # ----------------------------------------------------------------------

    ::oo::class create configurable {
	variable ""

	private {
	    method Properties {} {
		::info object properties [self] -all -readable
	    }
	    method WritableProperties {} {
		::info object properties [self] -all -writable
	    }

	    method GetRealOptionName {option} {
		::try {
		    ::return [::tcl::prefix match -message "option" \
				 [my Properties] $option]
		} on error msg {
		    # Convert errorCode
		    ::throw [::list TK LOOKUP OPTION $option] $msg
		}
	    }

	    method AllDescriptors {} {
		::lmap opt [my Properties] {
		    ::list $opt {*}[my <OptDescribe$opt>] $($opt)
		}
	    }

	    method OneDescriptor {option} {
		::set opt [my GetRealOptionName $option]
		::list $opt {*}[my <OptDescribe$opt>] $($opt)
	    }

	    method UpdateState opts {
		::set props [my WritableProperties]
		::set stateChanged 0
		::set state [::array get ""]
		::try {
		    ::foreach {option value} $opts {
			try {
			    ::set opt [::tcl::prefix match -message "option" \
					 $props $option]
			} on error {msg} {
			    ::try {
				::tcl::prefix match [my Properties] $option
			    } on error {} {
				# Do nothing
			    } on ok {optionName} {
				::set msg "read only option: $optionName"
			    }
			    ::throw [::list TK LOOKUP OPTION $option] $msg
			}
			::set ($opt) [my <OptValidate$opt> $value]
			::set stateChanged 1
		    }
		    ::if {$stateChanged} {
			my PostConfigure
		    }
		    ::unset -nocomplain state
		} finally {
		    # Rollback on error
		    ::if {[::info exists state]} {
			::array set "" $state
		    }
		}
	    }
	}

	method configure args {
	    ::try {
		::if {[::llength $args] == 0} {
		    ::return [my AllDescriptors]
		} elseif {[::llength $args] == 1} {
		    ::return [my OneDescriptor [::lindex $args 0]]
		} elseif {[::llength $args] % 2 == 0} {
		    my UpdateState $args
		    ::return
		} else {
		    # Don't like the genuine Tk errors; they're weird!
		    ::throw {TCL WRONGARGS} \
			[::format {wrong # args: should be "%s %s"} \
			     [lindex [info level 0] 0] \
			     "configure ?-option value ...?"]
		}
	    } on error {msg opt} {
		# Hide the implementation details
		::dict unset opt -errorinfo
		::dict incr opt -level
		::return -options $opt $msg
	    }
	}

	method cget {option} {
	    ::try {
		::return $([my GetRealOptionName $option])
	    } on error {msg opt} {
		# Hide the implementation details
		::dict unset opt -errorinfo
		::dict incr opt -level
		::return -options $opt $msg
	    }
	}

	method PostConfigure {} {}

	method Initialise {pathName args} {
	    ::if {[::llength $args] % 1} {
		# TODO: generate a better error
		::return -code error "wrong # args"
	    }
	    ::set toSet {}
	    ::set props [my Properties]
	    ::try {
		::foreach opt $props {
		    ::lassign [my <OptDescribe$opt>] nm cls def
		    ::set val [::option get $pathName $nm $cls]
		    ::if {$val eq ""} {
			::set val $def
		    }
		    ::dict set toSet $opt $val
		}
		::foreach {option value} $args {
		    ::try {
			# Tricky point: $props includes init-only options
			::set opt [::tcl::prefix match -message "option" \
				     $props $option]
		    } on error msg {
			::throw [::list TK LOOKUP OPTION $option] $msg
		    }
		    ::dict set toSet $opt [my <OptValidate$opt> $value]
		}
	    } on error {msg opt} {
		::dict unset opt -errorinfo
		::dict incr opt -level
		::return -options $opt $msg
	    }
	    # Apply the computed state
	    ::array set "" $toSet
	}
	forward Initialize my Initialise

	definitionnamespace -class ::tk::PropertyDefine

	# Individual widgets do not support defining their own properties.
	# This is different from ::oo::configurable
    }

    # ----------------------------------------------------------------------
    #
    # tk::megawidget --
    #
    #	The metaclass for making megawidgets (which are always configurable).
    #	Too bare at this point; intended to grow!
    #
    # ----------------------------------------------------------------------

    ::oo::class create megawidget {
	superclass ::oo::class

    	constructor {{definitionScript ""}} {
	    next {mixin ::tk::configurable}
	    next $definitionScript
	}
	definitionnamespace -class ::tk::PropertyDefine
    }

    # ----------------------------------------------------------------------
    #
    # tk::propertytype --
    #
    #	The class of types of properties. Abstract because concrete subclasses
    #	define how the validation is done.
    #
    # ----------------------------------------------------------------------

    ::oo::abstract create propertytype {
	private variable def

	constructor {default} {
	    set def $default
	    set map [namespace ensemble configure ::tk::PropType -map]
	    dict set map [namespace tail [self]] [self]
	    namespace ensemble configure ::tk::PropType -map $map
	}

	method validate {value} {
	    throw UNIMPLEMENTED "unimplemented method"
	}

	method Normalize {value} {
	    return $value
	}

	method default {} {
	    return $def
	}

	self {
	    unexport new create

	    private method ConditionName {name} {
		return [namespace current]::[namespace tail $name]
	    }

	    method createbool {name args} {
		tailcall ::tk::BoolTestType create \
		    [my ConditionName $name] {*}$args
	    }

	    method createthrow {name args} {
		tailcall ::tk::ThrowTestType create \
		    [my ConditionName $name] {*}$args
	    }

	    method createtable {name args} {
		tailcall ::tk::TableType create \
		    [my ConditionName $name] {*}$args
	    }
	}
    }

    ::oo::class create BoolTestType {
	superclass propertytype

	constructor {default test} {
	    next $default
	    ::oo::objdefine [self] forward Validate {*}$test
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
	    ::oo::objdefine [self] method Validate value $test
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
	    ::tcl::prefix match $Table $value
	}
    }

    # ----------------------------------------------------------------------
    #
    #	Install the actual types.
    #
    # ----------------------------------------------------------------------

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
