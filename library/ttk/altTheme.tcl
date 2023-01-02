#
# Ttk widget set: Alternate theme
#

namespace eval ttk::theme::alt {

    namespace import ::tk::ScaleNum

    variable colors
    array set colors {
	-frame 		"#d9d9d9"
	-window		"#ffffff"
        -alternate	"#f0f0f0"
	-darker 	"#c3c3c3"
	-border		"#414141"
	-activebg 	"#ececec"
	-disabledfg	"#a3a3a3"
	-selectbg	"#4a6984"
	-selectfg	"#ffffff"
	-altindicator	"#aaaaaa"
    }

    ttk::style theme settings alt {

	ttk::style configure "." \
	    -background 	$colors(-frame) \
	    -foreground 	black \
	    -troughcolor	$colors(-darker) \
	    -bordercolor	$colors(-border) \
	    -selectbackground 	$colors(-selectbg) \
	    -selectforeground 	$colors(-selectfg) \
	    -font 		TkDefaultFont \
	    ;

	ttk::style map "." -background \
	    [list disabled $colors(-frame)  active $colors(-activebg)] ;
	ttk::style map "." -foreground [list disabled $colors(-disabledfg)] ;
        ttk::style map "." -embossed [list disabled 1] ;

	ttk::style configure TButton \
	    -anchor center -width -11 -padding 0.75p \
	    -relief raised -shiftrelief 1 \
	    -highlightthickness 1 -highlightcolor $colors(-frame)
	ttk::style map TButton -relief {
	    {pressed !disabled}	sunken
	    {active !disabled}	raised
	} -highlightcolor {alternate black}

	set t [ScaleNum 2]; set r [ScaleNum 4]; set b $t
	set indMargin [list 0 $t $r $b]
	ttk::style configure TCheckbutton -indicatorcolor "#ffffff" \
	    -indicatormargin $indMargin -padding 1.5p
	ttk::style configure TRadiobutton -indicatorcolor "#ffffff" \
	    -indicatormargin $indMargin -padding 1.5p
	ttk::style map TCheckbutton -indicatorcolor \
	    [list  pressed $colors(-frame) \
	           alternate $colors(-altindicator) \
	           disabled $colors(-frame)]
	ttk::style map TRadiobutton -indicatorcolor \
	    [list  pressed $colors(-frame) \
	           alternate $colors(-altindicator) \
	           disabled $colors(-frame)]

	ttk::style configure TMenubutton \
	    -width -11 -arrowsize 3.75p \
	    -padding 2.5p -relief raised

	ttk::style configure TEntry -padding 1
	ttk::style map TEntry -fieldbackground \
		[list readonly $colors(-frame) disabled $colors(-frame)]

	ttk::style configure TCombobox -padding 1 -arrowsize 10.5p
	ttk::style map TCombobox -fieldbackground \
		[list readonly $colors(-frame) disabled $colors(-frame)] \
		-arrowcolor [list disabled $colors(-disabledfg)]
	ttk::style configure ComboboxPopdownFrame \
	    -relief solid -borderwidth 1

	set l [ScaleNum 2]; set r [ScaleNum 10]
	ttk::style configure TSpinbox -arrowsize 7.5p \
	    -padding [list $l 0 $r 0]
	ttk::style map TSpinbox -fieldbackground \
	    [list readonly $colors(-frame) disabled $colors(-frame)] \
	    -arrowcolor [list disabled $colors(-disabledfg)]

	ttk::style configure Toolbutton -relief flat -padding 1.5p
	ttk::style map Toolbutton -relief \
	    {disabled flat selected sunken pressed sunken active raised}
	ttk::style map Toolbutton -background \
	    [list pressed $colors(-darker)  active $colors(-activebg)]

	ttk::style configure TScrollbar -relief raised \
	    -arrowsize [ScaleNum 14] -width [ScaleNum 14]

	ttk::style configure TLabelframe -relief groove -borderwidth 2

	set l [ScaleNum 2]; set t $l; set r [ScaleNum 1]
	set margins [list $l $t $r 0]
	ttk::style configure TNotebook -tabmargins $margins
	ttk::style configure TNotebook.Tab -background $colors(-darker) \
	    -padding [list 3p 1.5p]
	ttk::style map TNotebook.Tab \
	    -background [list selected $colors(-frame)] \
	    -expand [list selected $margins]

	# Treeview:
	ttk::style configure Heading -font TkHeadingFont -relief raised
	ttk::style configure Treeview -background $colors(-window) \
                -stripedbackground $colors(-alternate)
	ttk::style configure Treeview.Separator \
                -background $colors(-alternate)
	ttk::style map Treeview \
	    -background [list disabled $colors(-frame)\
				selected $colors(-selectbg)] \
	    -foreground [list disabled $colors(-disabledfg) \
				selected $colors(-selectfg)]

	set thickness [ScaleNum 15]
	ttk::style configure TScale \
	    -groovewidth [ScaleNum 4] -troughrelief sunken \
	    -sliderthickness $thickness -borderwidth 2

	ttk::style configure TProgressbar \
	    -background $colors(-selectbg) -borderwidth 0 \
	    -barsize [ScaleNum 30] -thickness $thickness
    }

    unset l t r b indMargin margins thickness
}
