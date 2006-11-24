#
# $Id: clamTheme.tcl,v 1.2 2006/11/24 18:04:14 jenglish Exp $
#
# Ttk widget set: "Clam" theme
#
# Inspired by the XFCE family of Gnome themes.
#

namespace eval ttk::theme::clam {

    package provide ttk::theme::clam 0.0.1

    variable colors ; array set colors {
	-disabledfg	"#999999"

	-frame  	"#dcdad5"
	-dark		"#cfcdc8"
	-darker 	"#bab5ab"
	-darkest	"#9e9a91"
	-lighter	"#eeebe7"
	-lightest 	"#ffffff"
	-selectbg	"#4a6984"
	-selectfg	"#ffffff"
    }

    namespace import -force ::ttk::style
    style theme settings clam {

	style configure "." \
	    -background $colors(-frame) \
	    -foreground black \
	    -bordercolor $colors(-darkest) \
	    -darkcolor $colors(-dark) \
	    -lightcolor $colors(-lighter) \
	    -troughcolor $colors(-darker) \
	    -selectbackground $colors(-selectbg) \
	    -selectforeground $colors(-selectfg) \
	    -selectborderwidth 0 \
	    -font TkDefaultFont \
	    ;

	style map "." \
	    -background [list disabled $colors(-frame) \
			     active $colors(-lighter)] \
	    -foreground [list disabled $colors(-disabledfg)] \
	    -selectbackground [list  !focus $colors(-darkest)] \
	    -selectforeground [list  !focus white] \
	    ;
	# -selectbackground [list  !focus "#847d73"]

	style configure TButton -width -11 -padding 5 -relief raised
	style map TButton \
	    -background [list \
			     disabled $colors(-frame) \
			     pressed $colors(-darker) \
			     active $colors(-lighter)] \
	    -lightcolor [list pressed $colors(-darker)] \
	    -darkcolor [list pressed $colors(-darker)] \
	    -bordercolor [list alternate "#000000"] \
	    ;

	style configure Toolbutton -padding 2 -relief flat
	style map Toolbutton \
	    -relief {disabled flat selected sunken pressed sunken active raised} \
	    -background [list disabled $colors(-frame) \
			     pressed $colors(-darker) \
			     active $colors(-lighter)] \
	    -lightcolor [list pressed $colors(-darker)] \
	    -darkcolor [list pressed $colors(-darker)] \
	    ;

	style configure TCheckbutton \
	    -indicatorbackground "#ffffff" \
	    -indicatormargin {1 1 4 1} \
	    -padding 2 ;
	style configure TRadiobutton \
	    -indicatorbackground "#ffffff" \
	    -indicatormargin {1 1 4 1} \
	    -padding 2 ;
	style map TCheckbutton -indicatorbackground \
	    [list  disabled $colors(-frame)  pressed $colors(-frame)]
	style map TRadiobutton -indicatorbackground \
	    [list  disabled $colors(-frame)  pressed $colors(-frame)]

	style configure TMenubutton \
	    -width -11 -padding 5 -relief raised -anchor w

	style configure TEntry -padding 1 -insertwidth 1
	style map TEntry \
	    -background [list  readonly $colors(-frame)] \
	    -bordercolor [list  focus $colors(-selectbg)] \
	    -lightcolor [list  focus "#6f9dc6"] \
	    -darkcolor [list  focus "#6f9dc6"] \
	    ;

	style configure TCombobox -padding 1 -insertwidth 1
	style map TCombobox \
	    -background [list active $colors(-lighter) \
			     pressed $colors(-lighter)] \
	    -fieldbackground [list {readonly focus} $colors(-selectbg) \
				  readonly $colors(-frame)] \
	    -foreground [list {readonly focus} $colors(-selectfg)] \
	    ;

	style configure TNotebook.Tab -padding {6 2 6 2}
	style map TNotebook.Tab \
	    -padding [list selected {6 4 6 2}] \
	    -background [list selected $colors(-frame) {} $colors(-darker)] \
	    -lightcolor [list selected $colors(-lighter) {} $colors(-dark)] \
	    ;

    	style configure TLabelframe \
	    -labeloutside true -labelmargins {0 0 0 4} \
	    -borderwidth 2 -relief raised

	style configure TProgressbar -background $colors(-frame)

	style configure Sash -sashthickness 6 -gripcount 10
    }
}
