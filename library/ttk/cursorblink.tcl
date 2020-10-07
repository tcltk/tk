#!/usr/bin/tclsh

namespace eval ::ttk::cursorblink {

  proc getFromSystem { } {

    set ds $::env(DESKTOP_SESSION)

    # default is unknown
    set cursoron -1
    set cursoroff -1

    switch -exact $ds {
      xfce {
        set cursortm -1
        try {
          set cursorenabled [exec -ignorestderr \
              xfconf-query -c xsettings -p /Net/CursorBlink \
              2>/dev/null ]
          if { $cursorenabled } {
            set cursortm [exec -ignorestderr \
                xfconf-query -c xsettings -p /Net/CursorBlinkTime \
                2>/dev/null ]
          } else {
            set cursortm 0
          }
        } on error {err res} {
        }
        # GTK 3 divides the blink time by 3.
        # Two parts on-time and one part off-time.
        if { $cursortm != -1 } {
          set cursoron [expr {round(double($cursortm)*2.0/3.0)}]
          set cursoroff [expr {round(double($cursortm)/3.0)}]
        }
      }
      gnome -
      ubuntu -
      mate -
      cinnamon {
        # gnome also has an inactivity timeout for the cursor, but Tk
        # does not support that.

        set schema org.gnome.desktop.interface
        # they all use gsettings, but somehow have the need to create
        # their own schema for the same settings...
        switch -exact $ds {
          mate {
            set schema org.mate.interface
          }
          cinnamon {
            set schema org.cinnamon.desktop.interface
          }
          default {
          }
        }

        set cursortm -1
        try {
          set cursorenabled [exec -ignorestderr \
              gsettings get $schema cursor-blink \
              2>/dev/null ]
          if { $cursorenabled } {
            set cursortm [exec -ignorestderr \
                gsettings get $schema cursor-blink-time \
                2>/dev/null ]
          } else {
            set cursortm 0
          }
        } on error {err res} {
        }
        # GTK 3 divides the blink time by 3.
        # Two parts on-time and one part off-time.
        if { $cursortm != -1 } {
          set cursoron [expr {round(double($cursortm)*2.0/3.0)}]
          set cursoroff [expr {round(double($cursortm)/3.0)}]
        }
      }
      plasma -
      lxqt {
        set cursortm -1

        set confsearch [list]

        switch -exact $ds {
          plasma {
            # in KDE 5 the QT setting no longer works.
            # it is a manual setting in .config/kdeglobals
            if { ! [info exists ::env(KDE_SESSION_VERSION)] ||
                $::env(KDE_SESSION_VERSION) eq "5" } {
              lappend confsearch \
                  [file join $::env(HOME) .config kdeglobals] \
                  CursorBlinkRate
            }
            # KDE 4
            # kde's cursor blink is controlled by qt.
            # it has the same cursorFlashTime setting as lxqt, but
            # in a different configuration file.
            # qt4-qtconfig does not allow a setting of 0.  Presumably
            # a manual update would be necessary.
            if { ! [info exists ::env(KDE_SESSION_VERSION)] ||
                $::env(KDE_SESSION_VERSION) eq "4" } {
              lappend confsearch \
                  [file join $::env(HOME) .config Trolltech.conf] \
                  cursorFlashTime
            }
          }
          lxqt {
            lappend confsearch \
                [file join $::env(HOME) .config lxqt lxqt.conf] \
                cursorFlashTime
          }
        }

        foreach {conffn conftag} $confsearch {
          if { [file exists $conffn] } {
            set found false
            try {
              # may not exist in the configuration file.
              set cursortm [exec -ignorestderr \
                egrep ^${conftag} $conffn | \
                sed s,.*=,, \
                2>/dev/null ]
              if { $cursortm ne {} } {
                set found true
              }
            } on error { err res } {
            }
            if { $found } {
              break
            }
          }
        }
        if { $cursortm != -1 } {
          # The QT source code seems to split the blink time into two.
          # The timing is highly suspect based on empirical testing.
          # Try dividing by 4.
          set cursoron [expr {$cursortm/4}]
          set cursoroff $cursoron
        }
      }
      default {
      }
    }

    return [list $cursoron $cursoroff]
  }
}

# for testing
if { 0 } {
  lassign [::ttk::cursorblink::getFromSystem] cursoron cursoroff
  puts [list $cursoron $cursoroff]
}
