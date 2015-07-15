# blackTheme.tcl -
#
#   Experimental!
#
#  Copyright (c) 2007-2008 Mats Bengtsson
#  Copyright (c) 2015 <chw@ch-werner.de>

namespace eval ttk {
  namespace eval theme {
    namespace eval black {
      variable version 0.0.1
    }
  }
}

namespace eval ttk::theme::black {
  
  variable colors
  array set colors {
    -disabledfg	"DarkGrey"
    -frame  	"#424242"
    -dark	"#222222"
    -darker 	"#121212"
    -darkest	"black"
    -lighter	"#626262"
    -lightest 	"#ffffff"
    -selectbg	"#4a6984"
    -selectfg	"#ffffff"
  }

  ttk::style theme create black -parent clam -settings {
    
    # -----------------------------------------------------------------
    # Theme defaults
    #
    ttk::style configure "." \
        -background $colors(-frame) \
        -foreground white \
        -bordercolor $colors(-darkest) \
        -darkcolor $colors(-dark) \
        -lightcolor $colors(-lighter) \
        -troughcolor $colors(-darker) \
        -selectbackground $colors(-selectbg) \
        -selectforeground $colors(-selectfg) \
        -selectborderwidth 0 \
        -font TkDefaultFont \
        ;
    
    ttk::style map "." \
        -background [list disabled $colors(-frame) \
        active $colors(-lighter)] \
        -foreground [list disabled $colors(-disabledfg)] \
        -selectbackground [list  !focus $colors(-darkest)] \
        -selectforeground [list  !focus white] \
        ;
    
    # ttk widgets.
    ttk::style configure TButton \
        -width -8 -padding {5 1} -relief raised
    ttk::style configure TMenubutton \
        -width -11 -padding {5 1} -relief raised
    ttk::style configure TCheckbutton \
        -indicatorbackground "#ffffff" -indicatormargin {1 1 4 1}
    ttk::style configure TRadiobutton \
        -indicatorbackground "#ffffff" -indicatormargin {1 1 4 1}
    
    ttk::style configure TEntry \
        -fieldbackground $colors(-frame) -foreground white \
        -padding {2 0}
    ttk::style configure TCombobox \
        -fieldbackground $colors(-frame) -foreground white \
        -padding {2 0}
    
    ttk::style configure TNotebook.Tab \
        -padding {6 2 6 2}
    
    # tk widgets.
    ttk::style map Menu \
        -background [list active $colors(-lighter)] \
        -foreground [list disabled $colors(-disabledfg)]
    
    ttk::style configure TreeCtrl \
        -background gray30 -itembackground {gray60 gray50} \
        -itemfill white -itemaccentfill yellow

  }

  proc activated {} {

    if {[ttk::style theme use] ne "black"} {
      return
    }

    variable colors
    set prio widgetDefault

    option add *background $colors(-frame) $prio
    option add *foreground white $prio

    option add *selectBackground $colors(-selectbg) $prio
    option add *selectForeground $colors(-selectfg) $prio

    option add *activeBackground $colors(-darker) $prio
    option add *activeForeground white $prio

    option add *Text.borderWidth 0 $prio
    option add *Text.selectBorderWidth 0 $prio
    option add *Text.highlightThickness 2 $prio
    option add *Text.highlightColor $colors(-selectfg) $prio
    option add *Text.highlightBackground $colors(-selectbg) $prio
    option add *Text.inactiveSelectBackground $colors(-dark) $prio
    option add *Text.background $colors(-frame) $prio
    option add *Text.foreground white $prio

    option add *Entry.borderWidth 0 $prio
    option add *Entry.highlightThickness 2 $prio
    option add *Entry.highlightColor $colors(-selectfg) $prio
    option add *Entry.highlightBackground $colors(-selectbg) $prio
    option add *Entry.background $colors(-frame) $prio
    option add *Entry.foreground white $prio

    option add *Listbox.borderWidth 0 $prio
    option add *Listbox.highlightThickness 2 $prio
    option add *Listbox.highlightColor $colors(-selectfg) $prio
    option add *Listbox.highlightBackground $colors(-selectbg) $prio
    option add *Listbox.background $colors(-frame) $prio
    option add *Listbox.foreground white $prio

  }

  bind . <<ThemeChanged>> +ttk::theme::black::activated

}

package provide ttk::theme::black $::ttk::theme::black::version
