# button.tcl --
#
# This file defines the default bindings for Tk label, button,
# checkbutton, and radiobutton widgets and provides procedures
# that help in implementing those bindings.
#
# RCS: @(#) $Id: button.tcl,v 1.10 2000/05/25 17:19:57 ericm Exp $
#
# Copyright (c) 1992-1994 The Regents of the University of California.
# Copyright (c) 1994-1996 Sun Microsystems, Inc.
#
# See the file "license.terms" for information on usage and redistribution
# of this file, and for a DISCLAIMER OF ALL WARRANTIES.
#

#-------------------------------------------------------------------------
# The code below creates the default class bindings for buttons.
#-------------------------------------------------------------------------

if {[string match "macintosh" $tcl_platform(platform)]} {
    bind Radiobutton <Enter> {
	tkButtonEnter %W
    }
    bind Radiobutton <1> {
	tkButtonDown %W
    }
    bind Radiobutton <ButtonRelease-1> {
	tkButtonUp %W
    }
    bind Checkbutton <Enter> {
	tkButtonEnter %W
    }
    bind Checkbutton <1> {
	tkButtonDown %W
    }
    bind Checkbutton <ButtonRelease-1> {
	tkButtonUp %W
    }
}
if {[string match "windows" $tcl_platform(platform)]} {
    bind Checkbutton <equal> {
	tkCheckRadioInvoke %W select
    }
    bind Checkbutton <plus> {
	tkCheckRadioInvoke %W select
    }
    bind Checkbutton <minus> {
	tkCheckRadioInvoke %W deselect
    }
    bind Checkbutton <1> {
	tkCheckRadioDown %W
    }
    bind Checkbutton <ButtonRelease-1> {
	tkButtonUp %W
    }
    bind Checkbutton <Enter> {
	tkCheckRadioEnter %W
    }

    bind Radiobutton <1> {
	tkCheckRadioDown %W
    }
    bind Radiobutton <ButtonRelease-1> {
	tkButtonUp %W
    }
    bind Radiobutton <Enter> {
	tkCheckRadioEnter %W
    }
}
if {[string match "unix" $tcl_platform(platform)]} {
    bind Checkbutton <Return> {
	if {!$tk_strictMotif} {
	    tkCheckRadioInvoke %W
	}
    }
    bind Radiobutton <Return> {
	if {!$tk_strictMotif} {
	    tkCheckRadioInvoke %W
	}
    }
    bind Checkbutton <1> {
	tkCheckRadioInvoke %W
    }
    bind Radiobutton <1> {
	tkCheckRadioInvoke %W
    }
    bind Checkbutton <Enter> {
	tkButtonEnter %W
    }
    bind Radiobutton <Enter> {
	tkButtonEnter %W
    }
}

bind Button <space> {
    tkButtonInvoke %W
}
bind Checkbutton <space> {
    tkCheckRadioInvoke %W
}
bind Radiobutton <space> {
    tkCheckRadioInvoke %W
}

bind Button <FocusIn> {}
bind Button <Enter> {
    tkButtonEnter %W
}
bind Button <Leave> {
    tkButtonLeave %W
}
bind Button <1> {
    tkButtonDown %W
}
bind Button <ButtonRelease-1> {
    tkButtonUp %W
}

bind Checkbutton <FocusIn> {}
bind Checkbutton <Leave> {
    tkButtonLeave %W
}

bind Radiobutton <FocusIn> {}
bind Radiobutton <Leave> {
    tkButtonLeave %W
}

if {[string match "windows" $tcl_platform(platform)]} {

#########################
# Windows implementation 
#########################

# tkButtonEnter --
# The procedure below is invoked when the mouse pointer enters a
# button widget.  It records the button we're in and changes the
# state of the button to active unless the button is disabled.
#
# Arguments:
# w -		The name of the widget.

proc tkButtonEnter w {
    global tkPriv
    if {[string compare [$w cget -state] "disabled"] } {

	# If the mouse button is down, set the relief to sunken on entry.
	# Overwise, if there's an -overrelief value, set the relief to that.

	if {[string equal $tkPriv(buttonWindow) $w]} {
	    $w configure -state active -relief sunken
	} elseif { [string compare [$w cget -overrelief] ""] } {
	    set tkPriv(relief) [$w cget -relief]
	    $w configure -relief [$w cget -overrelief]
	}
    }
    set tkPriv(window) $w
}

# tkButtonLeave --
# The procedure below is invoked when the mouse pointer leaves a
# button widget.  It changes the state of the button back to
# inactive.  If we're leaving the button window with a mouse button
# pressed (tkPriv(buttonWindow) == $w), restore the relief of the
# button too.
#
# Arguments:
# w -		The name of the widget.

proc tkButtonLeave w {
    global tkPriv
    if {[string compare [$w cget -state] "disabled"]} {
	$w configure -state normal
    }

    # Restore the original button relief if the mouse button is down
    # or there is an -overrelief value.

    if {[string equal $tkPriv(buttonWindow) $w] || \
	    [string compare [$w cget -overrelief] ""] } {
	$w configure -relief $tkPriv(relief)
    }

    set tkPriv(window) ""
}

# tkCheckRadioEnter --
# The procedure below is invoked when the mouse pointer enters a
# checkbutton or radiobutton widget.  It records the button we're in
# and changes the state of the button to active unless the button is
# disabled.
#
# Arguments:
# w -		The name of the widget.

proc tkCheckRadioEnter w {
    global tkPriv
    if {[string compare [$w cget -state] "disabled"]} {
	if {[string equal $tkPriv(buttonWindow) $w]} {
	    $w configure -state active
	}
	if { [string compare [$w cget -overrelief] ""] } {
	    set tkPriv(relief) [$w cget -relief]
	    $w configure -relief [$w cget -overrelief]
	}
    }
    set tkPriv(window) $w
}

# tkButtonDown --
# The procedure below is invoked when the mouse button is pressed in
# a button widget.  It records the fact that the mouse is in the button,
# saves the button's relief so it can be restored later, and changes
# the relief to sunken.
#
# Arguments:
# w -		The name of the widget.

proc tkButtonDown w {
    global tkPriv
    # Only save the button's relief if it has no -overrelief value.  If there
    # is an overrelief setting, tkPriv(relief) will already have been set, and
    # the current value of the -relief option will be incorrect.

    if { [string equal [$w cget -overrelief] ""] } {
	set tkPriv(relief) [$w cget -relief]
    }

    if {[string compare [$w cget -state] "disabled"]} {
	set tkPriv(buttonWindow) $w
	$w configure -relief sunken -state active

	# If this button has a repeatdelay set up, get it going with an after
	after cancel $tkPriv(afterId)
	set delay [$w cget -repeatdelay]
	set tkPriv(repeated) 0
	if {$delay > 0} {
	    set tkPriv(afterId) [after $delay [list tkButtonAutoInvoke $w]]
	}
    }
}

# tkCheckRadioDown --
# The procedure below is invoked when the mouse button is pressed in
# a button widget.  It records the fact that the mouse is in the button,
# saves the button's relief so it can be restored later, and changes
# the relief to sunken.
#
# Arguments:
# w -		The name of the widget.

proc tkCheckRadioDown w {
    global tkPriv
    if { [string equal [$w cget -overrelief] ""] } {
	set tkPriv(relief) [$w cget -relief]
    }
    if {[string compare [$w cget -state] "disabled"]} {
	set tkPriv(buttonWindow) $w
	set tkPriv(repeated) 0
	$w configure -state active
    }
}

# tkButtonUp --
# The procedure below is invoked when the mouse button is released
# in a button widget.  It restores the button's relief and invokes
# the command as long as the mouse hasn't left the button.
#
# Arguments:
# w -		The name of the widget.

proc tkButtonUp w {
    global tkPriv
    if {[string equal $tkPriv(buttonWindow) $w]} {
	set tkPriv(buttonWindow) ""
	# Restore the button's relief.  If there is no overrelief, the
	# button relief goes back to its original value.  If there is an
	# overrelief, the relief goes to the overrelief (since the cursor is
	# still over the button).

	set relief [$w cget -overrelief]
	if { [string equal $relief  ""] } {
	    set relief $tkPriv(relief)
	}
	$w configure -relief $relief

	# Clean up the after event from the auto-repeater

	after cancel $tkPriv(afterId)

	if {[string equal $tkPriv(window) $w]
              && [string compare [$w cget -state] "disabled"]} {
	    $w configure -state normal

	    # Only invoke the command if it wasn't already invoked by the
	    # auto-repeater functionality
	    if { $tkPriv(repeated) == 0 } {
		uplevel #0 [list $w invoke]
	    }
	}
    }
}

}

if {[string match "unix" $tcl_platform(platform)]} {

#####################
# Unix implementation
#####################

# tkButtonEnter --
# The procedure below is invoked when the mouse pointer enters a
# button widget.  It records the button we're in and changes the
# state of the button to active unless the button is disabled.
#
# Arguments:
# w -		The name of the widget.

proc tkButtonEnter {w} {
    global tkPriv
    if {[string compare [$w cget -state] "disabled"]} {
	$w configure -state active

	# If the mouse button is down, set the relief to sunken on entry.
	# Overwise, if there's an -overrelief value, set the relief to that.

	if {[string equal $tkPriv(buttonWindow) $w]} {
	    $w configure -state active -relief sunken
	} elseif { [string compare [$w cget -overrelief] ""] } {
	    set tkPriv(relief) [$w cget -relief]
	    $w configure -relief [$w cget -overrelief]
	}
    }

    set tkPriv(window) $w
}

# tkButtonLeave --
# The procedure below is invoked when the mouse pointer leaves a
# button widget.  It changes the state of the button back to
# inactive.  If we're leaving the button window with a mouse button
# pressed (tkPriv(buttonWindow) == $w), restore the relief of the
# button too.
#
# Arguments:
# w -		The name of the widget.

proc tkButtonLeave w {
    global tkPriv
    if {[string compare [$w cget -state] "disabled"]} {
	$w configure -state normal
    }
    
    # Restore the original button relief if the mouse button is down
    # or there is an -overrelief value.

    if {[string equal $tkPriv(buttonWindow) $w] || \
	    [string compare [$w cget -overrelief] ""] } {
	$w configure -relief $tkPriv(relief)
    }

    set tkPriv(window) ""
}

# tkButtonDown --
# The procedure below is invoked when the mouse button is pressed in
# a button widget.  It records the fact that the mouse is in the button,
# saves the button's relief so it can be restored later, and changes
# the relief to sunken.
#
# Arguments:
# w -		The name of the widget.

proc tkButtonDown w {
    global tkPriv

    # Only save the button's relief if it has no -overrelief value.  If there
    # is an overrelief setting, tkPriv(relief) will already have been set, and
    # the current value of the -relief option will be incorrect.

    if { [string equal [$w cget -overrelief] ""] } {
	set tkPriv(relief) [$w cget -relief]
    }

    if {[string compare [$w cget -state] "disabled"]} {
	set tkPriv(buttonWindow) $w
	$w configure -relief sunken

	# If this button has a repeatdelay set up, get it going with an after
	after cancel $tkPriv(afterId)
	set delay [$w cget -repeatdelay]
	set tkPriv(repeated) 0
	if {$delay > 0} {
	    set tkPriv(afterId) [after $delay [list tkButtonAutoInvoke $w]]
	}
    }
}

# tkButtonUp --
# The procedure below is invoked when the mouse button is released
# in a button widget.  It restores the button's relief and invokes
# the command as long as the mouse hasn't left the button.
#
# Arguments:
# w -		The name of the widget.

proc tkButtonUp w {
    global tkPriv
    if {[string equal $w $tkPriv(buttonWindow)]} {
	set tkPriv(buttonWindow) ""

	# Restore the button's relief.  If there is no overrelief, the
	# button relief goes back to its original value.  If there is an
	# overrelief, the relief goes to the overrelief (since the cursor is
	# still over the button).

	set relief [$w cget -overrelief]
	if { [string equal $relief  ""] } {
	    set relief $tkPriv(relief)
	}
	$w configure -relief $relief

	# Clean up the after event from the auto-repeater
	after cancel $tkPriv(afterId)

	if {[string equal $w $tkPriv(window)] \
		&& [string compare [$w cget -state] "disabled"]} {

	    # Only invoke the command if it wasn't already invoked by the
	    # auto-repeater functionality
	    if { $tkPriv(repeated) == 0 } {
		uplevel #0 [list $w invoke]
	    }
	}
    }
}

}

if {[string match "macintosh" $tcl_platform(platform)]} {

####################
# Mac implementation
####################

# tkButtonEnter --
# The procedure below is invoked when the mouse pointer enters a
# button widget.  It records the button we're in and changes the
# state of the button to active unless the button is disabled.
#
# Arguments:
# w -		The name of the widget.

proc tkButtonEnter {w} {
    global tkPriv
    if {[string compare [$w cget -state] "disabled"]} {
	if {[string equal $w $tkPriv(buttonWindow)]} {
	    $w configure -state active
	} elseif { [string compare [$w cget -overrelief] ""] } {
	    set tkPriv(relief) [$w cget -relief]
	    $w configure -relief [$w cget -overrelief]
	}
    }
    set tkPriv(window) $w
}

# tkButtonLeave --
# The procedure below is invoked when the mouse pointer leaves a
# button widget.  It changes the state of the button back to
# inactive.  If we're leaving the button window with a mouse button
# pressed (tkPriv(buttonWindow) == $w), restore the relief of the
# button too.
#
# Arguments:
# w -		The name of the widget.

proc tkButtonLeave w {
    global tkPriv
    if {[string equal $w $tkPriv(buttonWindow)]} {
	$w configure -state normal
    }
    if { [string compare [$w cget -overrelief] ""] } {
	$w configure -relief $tkPriv(relief)
    }
    set tkPriv(window) ""
}

# tkButtonDown --
# The procedure below is invoked when the mouse button is pressed in
# a button widget.  It records the fact that the mouse is in the button,
# saves the button's relief so it can be restored later, and changes
# the relief to sunken.
#
# Arguments:
# w -		The name of the widget.

proc tkButtonDown w {
    global tkPriv

    if {[string compare [$w cget -state] "disabled"]} {
	set tkPriv(buttonWindow) $w
	$w configure -state active

	# If this button has a repeatdelay set up, get it going with an after
	after cancel $tkPriv(afterId)
	if { ![catch {$w cget -repeatdelay} delay] } {
	    set delay [$w cget -repeatdelay]
	    set tkPriv(repeated) 0
	    if {$delay > 0} {
		set tkPriv(afterId) [after $delay [list tkButtonAutoInvoke $w]]
	    }
	}
    }
}

# tkButtonUp --
# The procedure below is invoked when the mouse button is released
# in a button widget.  It restores the button's relief and invokes
# the command as long as the mouse hasn't left the button.
#
# Arguments:
# w -		The name of the widget.

proc tkButtonUp w {
    global tkPriv
    if {[string equal $w $tkPriv(buttonWindow)]} {
	$w configure -state normal
	set tkPriv(buttonWindow) ""

	if { [string compare [$w cget -overrelief] ""] } {
	    $w configure -relief [$w cget -overrelief]
	}

	# Clean up the after event from the auto-repeater
	after cancel $tkPriv(afterId)

	if {[string equal $w $tkPriv(window)]
              && [string compare [$w cget -state] "disabled"]} {
	    # Only invoke the command if it wasn't already invoked by the
	    # auto-repeater functionality
	    if { $tkPriv(repeated) == 0 } {
		uplevel #0 [list $w invoke]
	    }
	}
    }
}

}

##################
# Shared routines
##################

# tkButtonInvoke --
# The procedure below is called when a button is invoked through
# the keyboard.  It simulate a press of the button via the mouse.
#
# Arguments:
# w -		The name of the widget.

proc tkButtonInvoke w {
    if {[string compare [$w cget -state] "disabled"]} {
	set oldRelief [$w cget -relief]
	set oldState [$w cget -state]
	$w configure -state active -relief sunken
	update idletasks
	after 100
	$w configure -state $oldState -relief $oldRelief
	uplevel #0 [list $w invoke]
    }
}

# tkButtonAutoInvoke --
#
#	Invoke an auto-repeating button, and set it up to continue to repeat.
#
# Arguments:
#	w	button to invoke.
#
# Results:
#	None.
#
# Side effects:
#	May create an after event to call tkButtonAutoInvoke.

proc tkButtonAutoInvoke {w} {
    global tkPriv
    after cancel $tkPriv(afterId)
    set delay [$w cget -repeatinterval]
    if { [string equal $tkPriv(window) $w] } {
	incr tkPriv(repeated)
	uplevel #0 [list $w invoke]
    }
    if {$delay > 0} {
	set tkPriv(afterId) [after $delay [list tkButtonAutoInvoke $w]]
    }
}

# tkCheckRadioInvoke --
# The procedure below is invoked when the mouse button is pressed in
# a checkbutton or radiobutton widget, or when the widget is invoked
# through the keyboard.  It invokes the widget if it
# isn't disabled.
#
# Arguments:
# w -		The name of the widget.
# cmd -		The subcommand to invoke (one of invoke, select, or deselect).

proc tkCheckRadioInvoke {w {cmd invoke}} {
    if {[string compare [$w cget -state] "disabled"]} {
	uplevel #0 [list $w $cmd]
    }
}

