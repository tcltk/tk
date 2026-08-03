# consolecmd.tcl
#
# Lightweight stub, safe for auto_mkindex to source directly - it creates
# a namespace, sets one variable, and defines one proc. No interpreter
# creation, no Tk load, no window, no real side effects - just enough
# state so the proc knows where to find the real implementation once it's
# actually called.
#
# On first call, loads the real implementation (consolecmd.impl, given a
# non-.tcl extension deliberately so auto_mkindex's default *.tcl scan
# never sources it directly) and forwards the original call to it.

namespace eval ::tk::console {}
set ::tk::console::stubdir [file dirname [info script]]

proc console {args} {
    rename console {}
    uplevel #0 [list source [file join $::tk::console::stubdir consolecmd.impl]]
    tailcall console {*}$args
}
