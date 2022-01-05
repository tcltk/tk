# print.tcl --
#
# This demonstration script showcases the tk print commands.
#

if {![info exists widgetDemo]} {
    error "This script should be run from the \"widget\" demo."
}

set w .print
destroy $w
toplevel $w
wm title $w "Printing Demonstration"
positionWindow $w

image create photo logo -data {R0lGODlhMABLAPUAAP//////zP//mf//AP/MzP/Mmf/MAP+Zmf+ZZv+ZAMz//8zM/8zMzMyZzMyZmcyZZsyZAMxmZsxmM8xmAMwzM8wzAJnMzJmZzJmZmZlmmZlmZplmM5kzZpkzM5kzAGaZzGZmzGZmmWYzZmYzMzNmzDNmmTMzmTMzZgAzmQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAACH+BSAtZGwtACH5BAEKAAIALAAAAAAwAEsAAAb+QIFwSCwahY9HRMI8Op/JJVNSqVqv2OvjyRU8slbIJGwYg60S5ZR6jRi/4ITBOhkYIOd8dltEnAdmFQMJeoVXCEd/VnKGjRVOZ3NVgHlsjpBxVRCEYBIEAAARl4lgZmVgEQAKFx8Mo0ZnpqgAFyi2JqKGmGebWRIAILbCIo27cYFWASTCtievRXqSVwQfzLYeeYESxlnSVRIW1igjWHJmjBXbpKXeFQTizlh1eJNVHbYf0LGc39XW2PIoVZE0whasWPSqFBBHrkKEA3QG0DFTEMXBUsjCWesg4oMFAGwgtKsiwqA+jGiCiRPGAM6pLCVLGKHQ6EGJlc0IuDxzAgX+CCOW9DjAaUsEyAoT+GHpeSRoHgxEUWgAUEUpFhMWgTbKEPUBAU15TBZxekYD0RMEqCDLIpYIWTAcmGEd9rWQBxQyjeQqdK/ZTWEO3mK5l+9No75SrcHhm9WwnlzNoA5zdM+JHz0HCPQdUauZowoFnSw+c2CBvw6dUXT4LMKE6EIHUqMexgCiIREknOwl7Q+FhNQoLuzOc6Kw3kIIVOLqjYKBYCwinmgo9CBEswfMAziK7mRDoQhcUZxwoBKFibq3n3jXI0GyCPLC0DrS8GR1oaEoRBRYVhT99/qG4DcCA/yNU4Ajbjhhnx4P2DJggR3YZog6RyyYxwM9PSgMBaP+sQdgIRL0JAKBwnTooRMAFWLdiPyJ8JwvTnyQoh5midCASh149ZkTIFAmHnzOZOBfIU6U4Mhd4zF34DNEoDAhARGY50BvJkioyxFOGkKAShGkFsJwejiR5Xf8aZAaBp89coQJjuDXAQOApekEm45ANaAtIbyYxREf0OlICCK841uaahZBQjyfjXCACYjuaASjhFagRKSFNtloHg+hYWIxRohnBQWCSSAhBVZ+hkgRnlbxwJIVgIqGlaU6wkeTxHxjm6gVLImrFbHWVEQ1taZjWxJX7KqqnqgUEUxDwtqajrOaRkqhEDcxWwECbEjxTYe9gojqOJQ6JO231ob72bSqAjh4RgfsjiDCCfDCK8K8I9TL7r33nvGtCO7CO1dUAONk3LcBFxzwwEMwZ/DC4iAsRIE+CWNCbzeV8FfEtoDwVwnlacxMkcKQYIE/F5TQ2QcedUZCagyc3NsFGrXVZMipWVBCzKv4Q0JvCviDsjAwf4ylxBeX0KcwGs81ccgqGS3MBxc3RjDDVAvdBRcfeFy1MFd3bcQHJEQdlddkP5E1Cf9yXfbaV2d9RBAAOw==
}


pack [label $w.l -text "This demonstration showcases
        the tk print command. Clicking the buttons below
        print the data from the canvas and text widgets
        using platform-native dialogs."] -side top

pack [frame $w.m] -fill both -expand yes -side top

set c [canvas $w.m.c -bg white]
pack $c -fill both -expand no -side left

$c create rectangle 30 10 200 50 -fill blue -outline black
$c create oval 30 60 200 110 -fill green
$c create image 130 150 -image logo
$c create text 150 250   -anchor n -font {Helvetica 12}  \
	-text "A short demo of simple canvas elements."

set txt {
Tcl, or Tool Command Language, is an open-source multi-purpose C library which includes a powerful dynamic scripting language. Together they provide ideal cross-platform development environment for any programming project. It has served for decades as an essential system component in organizations ranging from NASA to Cisco Systems, is a must-know language in the fields of EDA, and powers companies such as FlightAware and F5 Networks.

Tcl is fit for both the smallest and largest programming tasks, obviating the need to decide whether it is overkill for a given job or whether a system written in Tcl will scale up as needed. Wherever a shell script might be used Tcl is a better choice, and entire web ecosystems and mission-critical control and testing systems have also been written in Tcl. Tcl excels in all these roles due to the minimal syntax of the language, the unique programming paradigm exposed at the script level, and the careful engineering that has gone into the design of the Tcl internals.
}

set t [text $w.m.t -wrap word]
pack $t -side right -fill both -expand no
$t insert end $txt

pack [frame $w.f] -side top -fill both -expand no
pack [button $w.f.b -text "Print Canvas" -command [list tk print $w.m.c]]  -expand no
pack [button $w.f.x -text "Print Text" -command [list tk print $w.m.t]]   -expand no

## See Code / Dismiss buttons
pack [addSeeDismiss $w.buttons $w] -side bottom -fill x


