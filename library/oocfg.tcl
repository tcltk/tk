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
    # ----------------------------------------------------------------------
    #
    # tk::OptionDefine --
    #
    #	The definition language namespace for configurable widget definitions.
    #	All actual implementations of bits and pieces are in the subordinate
    #	Support namespace, and the public API then namespace export/import-ed
    #	into the main namespace (which has its path set up to be a definition
    #	language).
    #
    # ----------------------------------------------------------------------

    namespace eval OptionDefine {
	namespace eval Support {
	    namespace ensemble create -command ::tk::OptionType
	    namespace path {::tcl ::tk ::oo::configuresupport}

	    # --------------------------------------------------------------
	    #
	    # tk::OptionDefine::Support::DefineOption --
	    #
	    #	Actually defines an option. Assumes all validation of
	    #	arguments has been done.
	    #
	    # --------------------------------------------------------------

	    proc DefineOption {name options} {
		set descriptor ::list
		lappend descriptor [dict get $options name]
		lappend descriptor [dict get $options class]
		set type [dict get $options type]
		if {[dict exists $options def]} {
		    lappend descriptor [dict get $options def]
		} else {
		    lappend descriptor [OptionType $type default]
		}
		set validator [list [namespace which OptionType] $type validate]

		uplevel 1 [list \
		    method <OptDescribe-$name> {} $descriptor]
		uplevel 1 [list \
		    forward <OptValidate-$name> {*}$validator]

		# We only make the access methods if they don't exist on this
		# class; this allows a user to override them before defining
		# the option.

		set meths [info class methods [uplevel 1 self] -scope unexported]
		if {"<OptRead-$name>" ni $meths} {
		    uplevel 1 [list \
			forward <OptRead-$name> my <StdOptRead> $name]
		}

		# Note: init-only options still have this
		if {"<OptWrite-$name>" ni $meths} {
		    uplevel 1 [list \
			forward <OptWrite-$name> my <StdOptWrite> $name]
		}

		uplevel 1 [list \
		    [namespace which readableproperties] -append -$name]
		if {![dict get $options init]} {
		    uplevel 1 [list \
			[namespace which writableproperties] -append -$name]
		}
	    }

	    # --------------------------------------------------------------
	    #
	    # tk::OptionDefine::Support::ListOptions --
	    #
	    #	Shorthand for getting the list of all options of a class of a
	    #	given type.
	    #
	    # --------------------------------------------------------------

	    proc ListOptions {class {type -readable}} {
		tailcall info class properties $class -all $type
	    }

	    # --------------------------------------------------------------
	    #
	    # tk::OptionDefine::Support::DefineAlias --
	    #
	    #	Actually defines a option alias. Does minor validation of its
	    #	target, checking that it actually exists and is not an
	    #	init-only option (because otherwise things get weird).
	    #
	    # --------------------------------------------------------------

	    proc DefineAlias {contextClass name otherName} {
		if {[string index $otherName 0] ne "-"} {
		    set otherName "-$otherName"
		}
		if {$otherName ni [ListOptions $contextClass]} {
		    throw [list TK LOOKUP OPTION $otherName] \
			"no such option \"$otherName\""
		}
		if {$otherName ni [ListOptions $contextClass -writable]} {
		    throw [list TK LOOKUP OPTION $otherName] \
			"may not alias init-only option \"$otherName\""
		}
		set descriptor [list ::list $otherName]

		uplevel 1 [list \
		    method <OptDescribe-$name> {} $descriptor]
		uplevel 1 [list \
		    forward <OptValidate-$name> my <OptValidate$otherName>]

		# Aliases always define readers and writers; overriding them
		# makes no sense at all!
		uplevel 1 [list \
		    forward <OptRead-$name> my <OptRead$otherName>]
		uplevel 1 [list \
		    forward <OptWrite-$name> my <OptWrite$otherName>]

		uplevel 1 [list \
		    [namespace which readableproperties] -append -$name]
		uplevel 1 [list \
		    [namespace which writableproperties] -append -$name]
	    }

	    # --------------------------------------------------------------
	    #
	    # tk::OptionDefine::Support::ParseOptionArgs --
	    #
	    #	Parses the various options to the [option] definition
	    #	command.
	    #
	    # --------------------------------------------------------------

	    proc ParseOptionArgs {name args} {
		dict set opt type string
		dict set opt class [string totitle $name]
		dict set opt name [string tolower $name]
		dict set opt init false
		set defFromType [OptionType string default]
		foreach {option value} $args {
		    switch [prefix match {
			-alias -class -default -initonly -name -type
		    } $option] {
			-alias {
			    if {[llength $args] != 2} {
				throw {TK OPTION_MISUSE} \
				    "-alias may only ever be used on its own"
			    }
			    dict set opt alias $value
			}
			-class {
			    if {![regexp {^[[:upper:]][[:alnum:]_]*$} $value]} {
				throw {TK OPTION_MISUSE} \
				    "-class must be alphanumeric with a leading capital letter"
			    }
			    dict set opt class $value
			}
			-name {
			    if {![regexp {^[[:lower:]][[:alnum:]_]*$} $value]} {
				throw {TK OPTION_MISUSE} \
				    "-name must be alphanumeric with a leading lower-case letter"
			    }
			    dict set opt name $value
			}
			-default {
			    # Can only validate this once we know the type
			    dict set opt def $value
			}
			-initonly {
			    # Use our existing boolean validator
			    dict set opt init \
				[OptionType boolean validate $value]
			}
			-type {
			    dict set opt type $value
			    set defFromType [OptionType $value default]
			}
		    }
		}
		if {[dict exists $opt def]} {
		    # Apply the type validation to the default
		    dict set opt def [OptionType [dict get $opt type] validate \
					  [dict get $opt def]]
		} else {
		    dict set opt def $defFromType
		}
		return $opt
	    }

	    # --------------------------------------------------------------
	    #
	    # tk::OptionDefine::Support::option --
	    #
	    #	The implementation of the [option] definition. Mostly
	    #	delegates to other procedures in this namespace.
	    #
	    # --------------------------------------------------------------

	    proc option {name args} {
		set contextClass [uplevel 1 self]
		if {[llength $args] % 2} {
		    return -code error -errorcode {TCL WRONGARGS} \
			[format {wrong # args: should be "%s"} \
			     "option name ?-option value ...?"]
		}
		if {![regexp -nocase {^[[:alpha:]][[:alnum:]_]*$} $name]} {
		    return -code error -errorcode {TK OPTION_NAME} \
			"bad option name \"$name\":\
		    	must be alphanumeric starting with a letter"
		}
		try {
		    set Opt [ParseOptionArgs $name {*}$args]
		    if {[dict exists $Opt alias]} {
			uplevel 1 [list \
			    [namespace which DefineAlias] $contextClass $name \
			    [dict get $Opt alias]]
		    } else {
			uplevel 1 [list \
			    [namespace which DefineOption] $name $Opt]
		    }
		} on error {msg opt} {
		    # Condition the errorinfo trace
		    dict unset opt -errorinfo
		    dict incr opt -level
		    return -options $opt $msg
		}
	    }

	    namespace export option
	}

	proc superclass args {
	    set support ::tk::ConfigurableStandardImplementations
	    uplevel 1 [list ::oo::define::superclass {*}$args]
	    tailcall ::oo::define::superclass -appendifnew $support
	}

	namespace import Support::option
	namespace export option
	namespace path ::oo::define
    }

    # ----------------------------------------------------------------------
    #
    # tk::Configurable --
    #
    #	The (mixin) class for configurable widgets.
    #
    #	Tricky point: namespace path of classes is uncertain; fully qualify
    #	everything.
    #
    # ----------------------------------------------------------------------

    ::oo::class create Configurable {
	private {
	    variable initialised

	    # --------------------------------------------------------------
	    #
	    # tk::Configurable Options --
	    #
	    #	Get the list of readable options of the object.
	    #
	    # --------------------------------------------------------------

	    method ReadableOptions {} {
		::info object properties [self] -all -readable
	    }

	    # --------------------------------------------------------------
	    #
	    # tk::Configurable WritableOptions --
	    #
	    #	Get the list of writable (after initialisation) options of the
	    #	object.
	    #
	    # --------------------------------------------------------------

	    method WritableOptions {} {
		::info object properties [self] -all -writable
	    }

	    # --------------------------------------------------------------
	    #
	    # tk::Configurable GetRealOptionName --
	    #
	    #	Expand unique prefixes of an option.
	    #
	    # --------------------------------------------------------------

	    method GetRealOptionName {option} {
		::try {
		    ::return [::tcl::prefix match -message "option" \
				 [my ReadableOptions] $option]
		} on error msg {
		    # Convert errorCode
		    ::throw [::list TK LOOKUP OPTION $option] $msg
		}
	    }

	    # --------------------------------------------------------------
	    #
	    # tk::Configurable DescribeOption --
	    #
	    #	Describes a single option (called from AllDescriptors and
	    #	OneDescriptor). The option name must be in its full form.
	    #
	    # --------------------------------------------------------------

	    method DescribeOption {option} {
		::set desc [my <OptDescribe$option>]
		::if {[::llength $desc] == 1} {
		    ::list $option {*}$desc
		} else {
		    ::list $option {*}$desc [my <OptRead$option>]
		}
	    }

	    # --------------------------------------------------------------
	    #
	    # tk::Configurable AllDescriptors --
	    #
	    #	Implements [$obj configure] with no extra arguments.
	    #
	    # --------------------------------------------------------------

	    method AllDescriptors {} {
		::lmap opt [my ReadableOptions] {my DescribeOption $opt}
	    }

	    # --------------------------------------------------------------
	    #
	    # tk::Configurable OneDescriptor --
	    #
	    #	Implements [$obj configure -opt] with no extra arguments.
	    #
	    # --------------------------------------------------------------

	    method OneDescriptor {option} {
		my DescribeOption [my GetRealOptionName $option]
	    }

	    # --------------------------------------------------------------
	    #
	    # tk::Configurable UpdateState --
	    #
	    #	Implements [$obj configure -opt val -opt val...].
	    #
	    # --------------------------------------------------------------

	    method UpdateState arguments {
		::set opts [my WritableOptions]
		::set stateChanged 0
		::set checkpoint [my <OptionsMakeCheckpoint>]
		::try {
		    ::foreach {option value} $arguments {
			try {
			    ::set opt [::tcl::prefix match -message "option" \
					 $opts $option]
			} on error {msg} {
			    ::try {
				::tcl::prefix match [my ReadableOptions] $option
			    } on error {} {
				# Do nothing
			    } on ok {optionName} {
				::set msg "read only option: $optionName"
			    }
			    ::throw [::list TK LOOKUP OPTION $option] $msg
			}
			set value [my <OptValidate$opt> $value]
			my <OptWrite$opt> $value
			::set stateChanged 1
		    }
		    ::if {$stateChanged} {
			my PostConfigure
		    }
		    ::unset -nocomplain checkpoint
		} finally {
		    # Rollback on error
		    ::if {[::info exists checkpoint]} {
			my <OptionsRestoreCheckpoint> $checkpoint
		    }
		}
	    }
	}

	# ------------------------------------------------------------------
	#
	# tk::Configurable configure --
	#
	#	Implements [$obj configure ...].
	#
	# ------------------------------------------------------------------

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

	# ------------------------------------------------------------------
	#
	# tk::Configurable cget --
	#
	#	Implements [$obj cget $option].
	#
	# ------------------------------------------------------------------

	method cget {option} {
	    ::try {
		::return [my <OptRead[my GetRealOptionName $option]>]
	    } on error {msg opt} {
		# Hide the implementation details
		::dict unset opt -errorinfo
		::dict incr opt -level
		::return -options $opt $msg
	    }
	}

	# ------------------------------------------------------------------
	#
	# tk::Configurable Initialise --
	#
	#	Initialisation version of [$obj configure], which reads the
	#	option database and will set init-only options as well as
	#	ordinary ones. Intended to be called from a constructor.
	#
	#	The actual method is called something else because it does
	#	forwarding trickery to track the spelling of how it has been
	#	called.
	#
	# ------------------------------------------------------------------

	method Impl<Init> {methodName pathName args} {
	    ::if {[info exists initialised] && $initialised} {
		::return -code error -errorcode {TK DOUBLE_INIT} \
		    "this object is already initialised"
	    }
	    ::if {[::llength $args] % 2} {
		# TODO: generate a more accurate error message
		::set call "my $methodName pathName ?-option value...?"
		::return -code error -errorcode {TCL WRONGARGS} \
		    "wrong # args: should be \"$call\""
	    }
	    ::set toSet {}
	    ::set opts [my ReadableOptions]
	    ::try {
		# Tricky point: we will be writing to read-only options here.
		::foreach opt $opts {
		    ::set desc [my <OptDescribe$opt>]
		    ::if {[llength $desc] == 1} {
			# Skip aliases
			::continue
		    }
		    ::lassign $desc nm cls def
		    ::set val [::option get $pathName $nm $cls $def]
		    ::if {[catch {my <OptValidate$opt> $val}]} {
			# If the user forces a bad value via the option DB,
			# use our default anyway. It's the best we can do and
			# the DB is (by *design*) not entirely under script
			# control.
			::set val $def
		    }
		    ::dict set toSet $opt $val
		}
		::foreach {option value} $args {
		    ::try {
			# Tricky point: $opts includes init-only options
			::set opt [::tcl::prefix match -message "option" \
				     $opts $option]
		    } on error msg {
			# Rewrite the error code
			::throw [::list TK LOOKUP OPTION $option] $msg
		    }
		    ::dict set toSet $opt [my <OptValidate$opt> $value]
		}
	    } on error {msg opt} {
		# Strip the error trace
		::dict unset opt -errorinfo
		::dict incr opt -level
		::return -options $opt $msg
	    }
	    # Apply the computed state
	    dict for {opt value} $toSet {
		my <OptWrite$opt> $value
	    }
	    ::set initialised true
	}
	forward Initialise my Impl<Init> Initialise
	forward Initialize my Impl<Init> Initialize

	# Individual widgets do not support defining their own options.
	# This is different from ::oo::configurable's properties.
    }

    # ----------------------------------------------------------------------
    #
    # tk::ConfigurableStandardImplementations --
    #
    #	The superclass with user-overridable parts of the configurable system.
    #
    #	Tricky point: namespace path of classes is uncertain; fully qualify
    #	everything.
    #
    # ----------------------------------------------------------------------

    ::oo::class create ConfigurableStandardImplementations {
	# ------------------------------------------------------------------
	#
	# tk::ConfigurableStandardImplementations <StdOptRead> --
	#
	#	How to actually read an option of a given name out of the
	#	state when using the standard model of storage (the array in
	#	the instance with the empty name).
	#
	# ------------------------------------------------------------------

	method <StdOptRead> {name} {
	    ::variable ""
	    ::return $($name)
	}

	# ------------------------------------------------------------------
	#
	# tk::ConfigurableStandardImplementations <StdOptWrite> --
	#
	#	How to actually write an option of a given name to the state
	#	when using the standard model of storage (the array in the
	#	instance with the empty name).
	#
	# ------------------------------------------------------------------

	method <StdOptWrite> {name value} {
	    ::variable ""
	    ::set ($name) $value
	}

	# ------------------------------------------------------------------
	#
	# tk::ConfigurableStandardImplementations <OptionsMakeCheckpoint> --
	#
	#	How to make a checkpoint of the state that can be restored if
	#	the configuration of the object fails. If overridden, the
	#	companion method <OptionsRestoreCheckpoint> should also be
	#	overridden. The format of checkpoints is undocumented
	#	formally, but this implementation uses a dictionary.
	#
	# ------------------------------------------------------------------

	method <OptionsMakeCheckpoint> {} {
	    ::variable ""
	    ::array get ""
	}

	# ------------------------------------------------------------------
	#
	# tk::ConfigurableStandardImplementations <OptionsRestoreCheckpoint> --
	#
	#	How to restore a checkpoint of the state because the
	#	configuration of the object has failed. If overridden, the
	#	companion method <OptionsMakeCheckpoint> should also be
	#	overridden. The format of checkpoints is undocumented
	#	formally, but this implementation uses a dictionary.
	#
	# ------------------------------------------------------------------

	method <OptionsRestoreCheckpoint> {checkpoint} {
	    ::variable ""
	    ::array set "" $checkpoint
	}

	# ------------------------------------------------------------------
	#
	# tk::ConfigurableStandardImplementations PostConfigure --
	#
	#	Hook for user code to find out when a state change really
	#	occurred with [$obj configure]. Does nothing by default;
	#	subclasses may change this.
	#
	# ------------------------------------------------------------------

	method PostConfigure {} {}
    }

    # ----------------------------------------------------------------------
    #
    # tk::configurable --
    #
    #	The metaclass for making megawidgets (which are always configurable).
    #	Too bare at this point; intended to grow!
    #
    # ----------------------------------------------------------------------

    ::oo::class create configurable {
	superclass ::oo::class

    	constructor {{definitionScript ""}} {
	    next {
		superclass ::tk::ConfigurableStandardImplementations
		mixin ::tk::Configurable
	    }
	    next $definitionScript
	}
	definitionnamespace -class ::tk::OptionDefine
    }

    # ----------------------------------------------------------------------
    #
    # tk::optiontype --
    #
    #	The class of types of options. Abstract because concrete subclasses
    #	define how the validation is done.
    #
    #	Provides two variables to subclasses that they may use:
    #	  * TypeName - the name of the type, cleaned up for display to users
    #	    in error messages.
    #	  * ErrorCode - a list describing the standard error code for problems
    #	    with parsing this type
    #
    # ----------------------------------------------------------------------

    ::oo::abstract create optiontype {
	private variable def name
	variable TypeName ErrorCode

	constructor {default} {
	    set def $default
	    set name [namespace tail [self]]
	    # Ugly hack! Trims *one* leading 'z' from the type name
	    set TypeName [regsub {^z} $name ""]
	    set ErrorCode [list TK VALUE [string toupper $TypeName]]
	    set map [namespace ensemble configure ::tk::OptionType -map]
	    dict set map $name [self]
	    namespace ensemble configure ::tk::OptionType -map $map
	}

	destructor {
	    set map [namespace ensemble configure ::tk::OptionType -map]
	    dict unset map $name
	    namespace ensemble configure ::tk::OptionType -map $map
	}

	# ------------------------------------------------------------------
	#
	# tk::optiontype validate --
	#
	#	How to validate that the sole argument (conventionally called
	#	'value') is a member of the type. Throws an error if it is not
	#	of the type. Also normalizes the value if it is of the type;
	#	for most types, this is a trivial no-change operation, but for
	#	some types it may be more significant (e.g., expanding a
	#	unique prefix with a table-driven type).
	#
	# ------------------------------------------------------------------

	method validate {value} {
	    throw UNIMPLEMENTED "unimplemented method"
	}

	# ------------------------------------------------------------------
	#
	# tk::optiontype default --
	#
	#	Produces the default value of the type. The rest of the code
	#	assumes that this is a constant, so it is recommended to be a
	#	zero or an empty string (where these are meaningful).
	#
	# ------------------------------------------------------------------

	method default {} {
	    return $def
	}

	self {
	    # --------------------------------------------------------------
	    #
	    # tk::optiontype Create --
	    #
	    #	Actual factory method. Wrapper that creates classes of the
	    #	correct implementation type with the right name.
	    #
	    # --------------------------------------------------------------

	    method Create {realClass name args} {
		# Condition the class name first
		set name [namespace current]::[namespace tail $name]
		tailcall $realClass create $name {*}$args
	    }

	    # --------------------------------------------------------------
	    #
	    # tk::optiontype createbool --
	    #
	    #	Create a option type that is driven by a boolean test.
	    #
	    # --------------------------------------------------------------

	    forward createbool my Create ::tk::BoolTestType

	    # --------------------------------------------------------------
	    #
	    # tk::optiontype createthrow --
	    #
	    #	Create a option type that is driven by an erroring test.
	    #
	    # --------------------------------------------------------------

	    forward createthrow my Create ::tk::ThrowTestType

	    # --------------------------------------------------------------
	    #
	    # tk::optiontype createtable --
	    #
	    #	Create a option type that is driven by a table of valid
	    #	values (effectively an enumeration, Tcl-style).
	    #
	    # --------------------------------------------------------------

	    forward createtable my Create ::tk::TableType
	}
    }

    ::oo::class create BoolTestType {
	superclass optiontype
	private variable
	variable TypeName ErrorCode

	constructor {default test {normalizer {}}} {
	    next $default
	    ::oo::objdefine [self] forward Validate {*}$test
	    if {$normalizer ne ""} {
		::oo::objdefine [self] method Normalize {value} $normalizer
	    }
	}

	method validate {value} {
	    if {![my Validate $value]} {
		return -code error -errorcode $ErrorCode \
		    "invalid $TypeName value \"$value\""
	    }
	    try {
		return [my Normalize $value]
	    } on error {msg opt} {
		return -code error -errorcode [dict get $opt -errorcode] $msg
	    }
	}

	method Normalize {value} {
	    return $value
	}
    }

    ::oo::class create ThrowTestType {
	superclass optiontype
	variable ErrorCode

	constructor {default test {normalizer {}}} {
	    next $default
	    ::oo::objdefine [self] method Validate value $test
	    if {$normalizer ne ""} {
		::oo::objdefine [self] method Normalize {value} $normalizer
	    }
	}

	method validate {value} {
	    try {
		my Validate $value
	    } on error {msg} {
		return -code error -errorcode $ErrorCode $msg
	    }
	    try {
		return [my Normalize $value]
	    } on error {msg opt} {
		return -code error -errorcode [dict get $opt -errorcode] $msg
	    }
	}

	method Normalize {value} {
	    return $value
	}
    }

    ::oo::class create TableType {
	superclass optiontype

	private variable Table Error
	variable TypeName ErrorCode
	constructor {default table} {
	    if {$default ni $table} {
		# This requires that the default be not an abbreviation
		error "default value \"$default\" not in table of licit values"
	    }
	    next $default
	    set Table $table
	    set Error [list -level 1 -errorcode $ErrorCode]
	}

	method validate {value} {
	    ::tcl::prefix match -message $TypeName -error $Error $Table $value
	}
    }

    # ----------------------------------------------------------------------
    #
    #	Install the actual types.
    #
    # ----------------------------------------------------------------------

    # Install the types: those with a boolean test
    optiontype createbool boolean "false" {
	string is boolean -strict
    } {lindex {true false} [expr {!$value}]}
    optiontype createbool zboolean "" {
	string is boolean
    } {
	if {$value ne ""} {
	    lindex {true false} [expr {!$value}]
	}
    }
    optiontype createbool integer "0" {
	string is entier -strict
    } {expr {[string trim $value] + 0}}
    optiontype createbool zinteger "" {
	string is entier
    } {
	set value [string trim $value]
	expr {$value eq "" ? "" : $value + 0}
    }
    optiontype createbool float "0.0" {
	string is double -strict
    } {expr {[string trim $value] + 0.0}}
    optiontype createbool zfloat "" {
	string is double
    } {
	set value [string trim $value]
	expr {$value eq "" ? "" : $value + 0.0}
    }
    optiontype createbool list "" {
	string is list
    } {list {*}$value}
    optiontype createbool dict "" {
	string is dict
    } {dict merge {} $value}
    optiontype createbool window "" {
	apply {value {expr {$value eq "" || [winfo exists $value]}}}
    }

    # Install the types: those with an erroring test
    oo::objdefine [optiontype createthrow string {} {}] {
	# Special case; everything valid
	method validate value {return $value}
    }
    optiontype createthrow distance "0p" {
	winfo fpixels . $value
    }
    optiontype createthrow image "" {
	if {$value ne ""} {
	    image type $value
	}
    }
    optiontype createthrow color "#000000" {
	winfo rgb . $value
    } {
	lassign [winfo rgb . $value] r g b
	format "#%02x%02x%02x" \
	    [expr {$r >> 8}] [expr {$g >> 8}] [expr {$b >> 8}]
    }
    optiontype createthrow zcolor "" {
	if {$value ne ""} {
	    winfo rgb . $value
	}
    } {
	if {$value ne ""} {
	    lassign [winfo rgb . $value] r g b
	    format "#%02x%02x%02x" \
		[expr {$r >> 8}] [expr {$g >> 8}] [expr {$b >> 8}]
	}
    }
    optiontype createthrow font "TkDefaultFont" {
	# Cheapest property of fonts to read
	font metrics $value -fixed
    }
    optiontype createthrow cursor "" {
	if {$value ne ""} {
	    ::tk::ParseCursor $value
	}
    }

    # Install the types: those with an element table
    optiontype createtable anchor "center" {
	n ne e se s sw w nw center
    }
    optiontype createtable justify "left" {
	center left right
    }
    optiontype createtable relief "flat" {
	flat groove raised ridge solid sunken
    }
}

# Local Variables:
# mode: tcl
# c-basic-offset: 4
# fill-column: 78
# End:
