#!/usr/bin/tclsh

namespace eval ::ttk::cursorblink {

  proc getFromSystem { } {

    set ds $::env(DESKTOP_SESSION)

    # default is unknown
    set cursoron -1
    set cursoroff -1

    switch -exact $ds {
      xfce {
        try {
          set cursorenabled [exec \
              xfconf-query -c xsettings -p /Net/CursorBlink]
          if { $cursorenabled } {
            set cursoron [exec \
                xfconf-query -c xsettings -p /Net/CursorBlinkTime]
            set cursoroff $cursoron
          } else {
            set cursoroff 0
          }
        } on error {err res} {
        }
      }
      gnome -
      ubuntu -
      mate -
      cinnamon {
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
        try {
          set cursorenabled [exec \
              gsettings get $schema cursor-blink]
          if { $cursorenabled } {
            set cursoron [exec \
                gsettings get $schema cursor-blink-time]
            set cursoroff $cursoron
            # gnome also has an inactivity timeout for the cursor, but Tk
            # does not support that.
          } else {
            set cursoroff 0
          }
        } on error {err res} {
        }
      }
      plasma -
      lxqt {
        set cursortm -1

        switch -exact $ds {
          plasma {
            # kde's cursor blink is controlled by qt.
            # it has the same cursorFlashTime setting as lxqt, but
            # in a different configuration file.
            # qt4-qtconfig does not allow a setting of 0.  Presumably
            # a manual update would be necessary.
            set conffn [file join $::env(HOME) .config Trolltech.conf]
          }
          lxqt {
            set conffn [file join $::env(HOME) .config lxqt lxqt.conf]
          }
        }

        # may not exist in the configuration file.
        try {
          set cursortm [exec \
            egrep ^cursorFlashTime $conffn | \
            sed s,.*=,,]
        } on error { err res } {
        }
        if { $cursortm != -1 } {
          # cursorFlashTime claims to be in ms, but the timing is highly suspect.
          # A setting of 1000 ms is far less than 1 second, and 10000 ms is
          # more than 1 second.
          # It is set to 0 for no blink.
          set cursoron [expr {$cursortm/10}]
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
