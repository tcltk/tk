/* 
 * tkInitScript.h --
 *
 *	This file contains Unix & Windows common init script
 *      It is not used on the Mac. (the mac init script is in tkMacInit.c)
 *
 * Copyright (c) 1997 Sun Microsystems, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * SCCS: @(#) tkInitScript.h 1.3 97/08/11 19:12:28
 */

/*
 * In order to find tk.tcl during initialization, the following script
 * is invoked by Tk_Init().  It looks in several different directories:
 *
 *	$tk_library		- can specify a primary location, if set
 *				  no other locations will be checked
 *
 *	$env(TK_LIBRARY)	- highest priority so user can always override
 *				  the search path unless the application has
 *				  specified an exact directory above
 *
 *	$tcl_library/../tk$tk_version
 *				- look relative to init.tcl in an installed
 *				  lib directory (e.g. /usr/local)
 *
 *	<executable directory>/../lib/tk$tk_version
 *				- look for a lib/tk<ver> in a sibling of
 *				  the bin directory (e.g. /usr/local)
 *
 *	<executable directory>/../library
 *				- look in Tk build directory
 *
 *	<executable directory>/../../tk$tk_patchLevel/library
 *				- look for Tk build directory relative
 *				  to a parallel build directory
 *
 * The first directory on this path that contains a valid tk.tcl script
 * will be set ast the value of tk_library.
 *
 * Note that this entire search mechanism can be bypassed by defining an
 * alternate tkInit procedure before calling Tk_Init().
 */

static char initScript[] = "if {[info proc tkInit]==\"\"} {\n\
  proc tkInit {} {\n\
    global tk_library tk_version tk_patchLevel env errorInfo\n\
    rename tkInit {}\n\
    set errors {}\n\
    set dirs {}\n\
    if {[info exists tk_library]} {\n\
	lappend dirs $tk_library\n\
    } else {\n\
	if {[info exists env(TK_LIBRARY)]} {\n\
	    lappend dirs $env(TK_LIBRARY)\n\
	}\n\
	lappend dirs [file join [file dirname [info library]] tk$tk_version]\n\
	set parentDir [file dirname [file dirname [info nameofexecutable]]]\n\
	lappend dirs [file join $parentDir lib tk$tk_version]\n\
	lappend dirs [file join $parentDir library]\n\
	if [string match {*[ab]*} $tk_patchLevel] {\n\
	    set ver $tk_patchLevel\n\
	} else {\n\
	    set ver $tk_version\n\
	}\n\
	lappend dirs [file join [file dirname $parentDir] tk$ver/library]\n\
    }\n\
    foreach i $dirs {\n\
	set tk_library $i\n\
	set tkfile [file join $i tk.tcl]\n\
        if {[interp issafe] || [file exists $tkfile]} {\n\
	    if {![catch {uplevel #0 [list source $tkfile]} msg]} {\n\
		return\n\
	    } else {\n\
		append errors \"$tkfile: $msg\n$errorInfo\n\"\n\
	    }\n\
	}\n\
    }\n\
    set msg \"Can't find a usable tk.tcl in the following directories: \n\"\n\
    append msg \"    $dirs\n\n\"\n\
    append msg \"$errors\n\n\"\n\
    append msg \"This probably means that Tk wasn't installed properly.\n\"\n\
    error $msg\n\
  }\n\
}\n\
tkInit";

