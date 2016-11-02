# droidTheme.tcl
#
# A sample pixmap theme for the tile package.
# Derived from plastik theme.
#
#  Copyright (c) 2004 Googie
#  Copyright (c) 2005 Pat Thoyts <patthoyts@users.sourceforge.net>
#  Copyright (c) 2014 <chw@ch-werner.de>

namespace eval ttk::theme::droid {

    variable colors
    array set colors {
    	-frame 			"#e6e6e6"
	-window			"#ffffff"
	-activebg		"#e6e6e6"
	-troughbg		"#657a9e"
	-selectbg		"#657a9e"
	-selectfg		"#ffffff"
	-disabledfg		"#aaaaaa"
	-indicator		"#b03060"
	-altindicator		"#657a9e"
	-disabledaltindicator	"#657a9e"
    }

    set colors(-frame) [. cget -bg]

    variable hover hover
    variable dpi 120
    if {$::ttk::theme::default::sdltk} {
	set dpi $::ttk::theme::default::dpi
    }
    if {$dpi < 140} {
	set dpi 120
    } elseif {$dpi < 240} {
	set dpi 160
    } elseif {$dpi < 320} {
	set dpi 240
    } elseif {$dpi < 400} {
	set dpi 320
    } else {
	set dpi 400
    }
    if {[package vsatisfies [package present Ttk] 8-8.5.9] || \
	[package vsatisfies [package present Ttk] 8.6-8.6b1]} {
	# The hover state is not supported prior to 8.6b1 or 8.5.9
	set hover active
    }

    proc LoadImages {imgdir dpi} {
        variable I
        if {![interp issafe]} {
            set imgdir [file normalize $imgdir]
        }
        foreach file [glob -directory $imgdir *.gif] {
            set img [file tail [file rootname $file]]
            set I($img) [image create photo -file $file]
        }
        foreach file [glob -directory [file join $imgdir $dpi] *.gif] {
            set img [file tail [file rootname $file]]
            set I($img) [image create photo -file $file]
        }
    }

    LoadImages [file join [file dirname [info script]] droid] $dpi

    option add *selectForeground $colors(-selectfg)
    option add *selectBackground $colors(-selectbg)

    variable prio
    set prio widgetDefault

    option add *Text.borderWidth 0 $prio
    option add *Text.highlightThickness 2 $prio
    option add *Text.highlightColor #809fbd $prio
    option add *Text.highlightBackground #acacac $prio
    option add *Text.background #ffffff $prio

    option add *Entry.borderWidth 0 $prio
    option add *Entry.highlightThickness 2 $prio
    option add *Entry.highlightColor #809fbd $prio
    option add *Entry.highlightBackground #acacac $prio
    option add *Entry.background #ffffff $prio

    option add *Listbox.borderWidth 0 $prio
    option add *Listbox.highlightThickness 2 $prio
    option add *Listbox.highlightColor #809fbd $prio
    option add *Listbox.highlightBackground #acacac $prio
    option add *Listbox.background #ffffff $prio

    ttk::style theme settings droid {
	ttk::style configure . \
	    -background $colors(-frame) \
	    -troughcolor $colors(-frame) \
	    -selectbackground $colors(-selectbg) \
	    -selectforeground $colors(-selectfg) \
	    -fieldbackground $colors(-window) \
	    -borderwidth 1 \
	    -font TkDefaultFont \
	    ;

	ttk::style map . -foreground [list disabled $colors(-disabledfg)]

	ttk::style configure TScrollbar -borderwidth 0 \
	    -arrowsize 0

	#
	# Layouts:
	#
	ttk::style layout Vertical.TScrollbar {
	    Vertical.Scrollbar.trough -sticky ns -children {
		Vertical.Scrollbar.thumb -expand 1 -unit 1 -children {
		    Vertical.Scrollbar.grip -sticky ns
		}
	    }
	}

	ttk::style layout Horizontal.TScrollbar {
	    Horizontal.Scrollbar.trough -sticky ew -children {
		Horizontal.Scrollbar.thumb -expand 1 -unit 1 -children {
		    Horizontal.Scrollbar.grip -sticky ew
		}
	    }
	}

	ttk::style layout TButton {
	    Button.button -children {
		Button.padding -children {
		    Button.label -side left -expand true
		}
	    }
	}

	ttk::style layout TCheckbutton {
	    Checkbutton.button -children {
		Checkbutton.padding -expand true -children {
		    Checkbutton.indicator -side left
		    Checkutton.label -side left
		}
	    }
	}

	ttk::style layout TRadiobutton {
	    Radiobutton.button -children {
		Radiobutton.padding -expand true -children {
		    Radiobutton.indicator -side left
		    Radiobutton.label -side left
		}
	    }
	}

	ttk::style layout Toolbutton {
	    Toolbutton.border -children {
		Toolbutton.button -children {
		    Toolbutton.padding -children {
			Toolbutton.label -side left -expand true
		    }
		}
	    }
	}

	ttk::style layout TMenubutton {
	    Menubutton.button -children {
		Menubutton.indicator -side right
		Menubutton.padding -children {
		    Menubutton.label -side left -expand true
		}
	    }
	}

	#
	# Elements:
	#
	ttk::style element create Button.button image \
	    [list $I(button-n) pressed $I(button-p)] \
	    -border {4 10} -padding {4 6} -sticky ewns

	ttk::style element create Toolbutton.button image \
	    [list $I(tbutton-n) selected $I(tbutton-p) \
		 pressed $I(tbutton-p)] \
	    -border {4 9} -padding 3 -sticky news

	ttk::style element create Checkbutton.indicator image \
	    [list $I(check-nu) {pressed selected} $I(check-pc) \
		 pressed $I(check-hc) \
		 {!disabled selected} $I(check-nc) \
		 {disabled !selected} $I(check-nu) \
		 {disabled selected} $I(check-pc) \
		 {!disabled alternate} $I(check-nu) \
		 {disabled alternate} $I(check-pc)] \
	    -sticky {}

	ttk::style element create Radiobutton.indicator image \
	    [list $I(radio-nu) {pressed selected} $I(radio-pc) \
		 pressed $I(radio-hc) \
		 {!disabled selected} $I(radio-nc) \
		 {disabled !selected} $I(radio-nu) \
		 {disabled selected} $I(radio-pc) \
		 {!disabled alternate} $I(radio-nu) \
		 {disabled alternate} $I(radio-pc)] \
	    -sticky {}

	ttk::style element create Horizontal.Scrollbar.thumb \
	    image $I(hsb-n) -border 3 -sticky ew
	ttk::style element create Horizontal.Scrollbar.grip \
	    image $I(hsb-g) -sticky ew
	ttk::style element create Vertical.Scrollbar.thumb \
	    image $I(vsb-n) -border 3 -sticky ns
	ttk::style element create Vertical.Scrollbar.grip \
	    image $I(vsb-g) -sticky ns

	ttk::style element create Horizontal.Scale.slider \
	    image $I(slider-n) -sticky {}
	ttk::style element create Horizontal.Scale.trough \
	    image $I(hslider-t) -border {2 1} -padding 0
	ttk::style element create Vertical.Scale.slider \
	    image $I(slider-n) -sticky {}
	ttk::style element create Vertical.Scale.trough \
	    image $I(vslider-t) -border {1 2} -padding 0

	ttk::style element create Entry.field image \
	    [list $I(entry-n) focus $I(entry-f)] \
	    -border 2 -padding {3 4} -sticky news

	ttk::style element create Labelframe.border image \
	    $I(border) -border 4 -padding 4 -sticky news

	ttk::style element create Menubutton.button \
	    image [list $I(combo-r) active $I(combo-ra)] \
	    -sticky news -border {4 6 34 15} -padding {4 4 5}
	ttk::style element create Menubutton.indicator \
	    image [list $I(arrow-n) disabled $I(arrow-d)] \
	    -sticky e -border {25 0 0 0}

	ttk::style element create Combobox.field image \
	    [list $I(combo-n) [list readonly $hover !disabled] $I(combo-ra) \
		 [list focus $hover !disabled]	$I(combo-fa) \
		 [list $hover !disabled] $I(combo-a) \
		 [list !readonly focus !disabled] $I(combo-f) \
		 [list !readonly disabled] $I(combo-d) \
		 readonly $I(combo-r)] \
	    -border {4 6 34 15} -padding {4 4 5} -sticky news
	ttk::style element create Combobox.downarrow image \
	    [list $I(arrow-n) disabled $I(arrow-d)] \
	    -sticky e -border {15 0 0 0}

	ttk::style element create Notebook.client image \
	    $I(notebook-c) -border 4
	ttk::style element create Notebook.tab image \
	    [list $I(notebook-tn) selected $I(notebook-ts)] \
	    -padding {0 2 0 0} -border {4 10 4 10}

	ttk::style element create Progressbar.trough \
	    image $I(hprogress-t) -border 2
	ttk::style element create Horizontal.Progressbar.pbar \
	    image $I(hprogress-b) -border {2 9}
	ttk::style element create Vertical.Progressbar.pbar \
	    image $I(vprogress-b) -border {9 2}

	ttk::style element create Treeheading.cell \
	    image [list $I(tree-n) pressed $I(tree-p)] \
	    -border {4 10} -padding 4 -sticky ewns

	# Use the treeview item indicator from the alt theme,
	# as that looks better
	ttk::style element create Treeitem.indicator from alt

	#
	# Settings:
	#
	ttk::style configure TButton -width -10 -anchor center
	ttk::style configure Toolbutton -anchor center
	ttk::style configure TNotebook -tabmargins {0 2 0 0}
	ttk::style configure TNotebook.Tab -padding {6 2 6 2} -expand {0 0 2}
	ttk::style map TNotebook.Tab -expand [list selected {1 0 4 2}]

	# Spinbox (only available since 8.6b1 or 8.5.9)
	ttk::style layout TSpinbox {
	    Spinbox.field -side top -sticky we -children {
		Spinbox.buttons -side right -border 1 -children {
		    null -side right -sticky {} -children {
			Spinbox.uparrow -side top -sticky e
			Spinbox.downarrow -side bottom -sticky e
		    }
		}
		Spinbox.padding -sticky nswe -children {
		    Spinbox.textarea -sticky nswe
		}
	    }
	}

	ttk::style element create Spinbox.field \
	    image [list $I(spinbox-n) focus $I(spinbox-f)] \
	    -border {2 2 18 2} -padding {3 0 0} -sticky news
	ttk::style element create Spinbox.buttons \
	    image [list $I(spinbut-n) [list $hover !disabled] $I(spinbut-a)] \
	    -border {5 3 3} -padding {0 0 1 0}
	ttk::style element create Spinbox.uparrow image \
	    [list $I(spinup-n) disabled $I(spinup-d) pressed $I(spinup-p)]
	ttk::style element create Spinbox.downarrow image \
	    [list $I(spindown-n) disabled $I(spindown-d) pressed $I(spindown-p)]
	ttk::style element create Spinbox.padding image $I(spinbut-n) \
	    -border {0 3}
    
	# Treeview (since 8.6b1 or 8.5.9)
	ttk::style configure Treeview -background $colors(-window)
	#    -rowheight [font metrics TkDefaultFont -linespace]
	ttk::style map Treeview \
	    -background [list selected $colors(-selectbg)] \
	    -foreground [list selected $colors(-selectfg)]

	# Treeview (older version)
	ttk::style configure Row -background $colors(-window)
	ttk::style configure Cell -background $colors(-window)
	ttk::style map Row \
	    -background [list selected $colors(-selectbg)] \
	    -foreground [list selected $colors(-selectfg)]
	ttk::style map Cell \
	    -background [list selected $colors(-selectbg)] \
	    -foreground [list selected $colors(-selectfg)]
	ttk::style map Item \
	    -background [list selected $colors(-selectbg)] \
	    -foreground [list selected $colors(-selectfg)]
    }
}

