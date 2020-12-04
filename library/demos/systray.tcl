# systray.tcl --
#
# This demonstration script showcases the tk systray and tk sysnotify commands.
# 

if {![info exists widgetDemo]} {
    error "This script should be run from the \"widget\" demo."
}


set w .systray
destroy $w
toplevel $w
wm title $w "System Tray Demonstration"
positionWindow $w

set iconmenu .menubar
destroy $iconmenu
menu $iconmenu
$iconmenu add command -label "Status" -command { puts "status icon clicked" }
$iconmenu add command -label "Exit" -command exit

pack [label $w.l -text "This demonstration showcases \nthe tk systray and tk sysnotify commands\nRunning this demo creates the systray icon.\nClicking the buttons below modifies and destroys the icon\nand displays the notification."]

image create photo book -data R0lGODlhDwAPAKIAAP//////AP8AAMDAwICAgAAAAAAAAAAAACwAAAAADwAPAAADSQhA2u5ksPeKABKSCaya29d4WKgERFF0l1IMQCAKatvBJ0OTdzzXI1xMB3TBZAvATtB6NSLKleXi3OBoLqrVgc0yv+DVSEUuFxIAOw==

pack [button $w.b1 -text "Modify Tray Icon" -command modify]
pack [button $w.b3 -text "Destroy Tray Icon" -command {tk systray destroy}]
pack [button $w.b2 -text "Display Notification" -command notify]


tk systray create -image book -text "Systray sample" -button1 {puts "foo"} -button3 {tk_popup $iconmenu [winfo pointerx .] [winfo pointery .]}


proc modify { } {

    image create photo page -data R0lGODlhCwAPAKIAAP//////AMDAwICAgAAA/wAAAAAAAAAAACwAAAAACwAPAAADMzi6CzAugiAgDGE68aB0RXgRJBFVX0SNpQlUWfahQOvSsgrX7eZJMlQMWBEYj8iQchlKAAA7

    tk systray configure -image page
    tk systray configure -text "Modified text"
    tk systray configure -button1 {puts "this is a different output"}
    tk systray configure -button3 {puts "hello yall"}

}


proc notify {} {
    tk sysnotify  "Alert" "This is an alert"
}


## See Code / Dismiss buttons
pack [addSeeDismiss $w.buttons $w] -side bottom -fill x


    
