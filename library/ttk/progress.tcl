#
# Ttk widget set: progress bar utilities.
#

namespace eval ttk::progressbar {

    variable Timers	;# Map: widget name -> after ID

    # Default font for -text option in progressbars:
    # The size of a progressbar takes the font height into account so that when
    # some -text is to be displayed it does not get cropped. The default font
    # size must be very small in order to not increase the progresbar height
    # to the font height when no text is displayed (which is the default).
    # When some -text must be displayed then the user has to set an adequate
    # font size. For more details, see ticket [8bee4b2009].
    font create TkDefaultFont_progressbar {*}[font actual TkDefaultFont]
    font configure TkDefaultFont_progressbar -size -1
}

# Autoincrement --
#	Periodic callback procedure for autoincrement mode
#
proc ttk::progressbar::Autoincrement {pb steptime stepsize} {
    variable Timers

    if {![winfo exists $pb]} {
	# widget has been destroyed -- cancel timer
	unset -nocomplain Timers($pb)
	return
    }

    set Timers($pb) [after $steptime \
	[list ttk::progressbar::Autoincrement $pb $steptime $stepsize] ]

    $pb step $stepsize
}

# ttk::progressbar::start --
#	Start autoincrement mode.  Invoked by [$pb start] widget code.
#
proc ttk::progressbar::start {pb {steptime 50} {stepsize 1}} {
    variable Timers
    if {![info exists Timers($pb)]} {
	Autoincrement $pb $steptime $stepsize
    }
    if {[tk windowingsystem] eq "aqua"} {
	$pb state selected
    }
}

# ttk::progressbar::stop --
#	Cancel autoincrement mode. Invoked by [$pb stop] widget code.
#
proc ttk::progressbar::stop {pb} {
    variable Timers
    if {[info exists Timers($pb)]} {
	after cancel $Timers($pb)
	unset Timers($pb)
    }
    $pb configure -value 0
    if {[tk windowingsystem] eq "aqua"} {
	$pb state !selected
    }
}


