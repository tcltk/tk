# constraints.tcl --
#
# This file is sourced by each test file when invoking "tcltest::loadTestedCommands".
# It defines test constraints that are used by several test files in the
# Tk test suite.
#
# See the file "license.terms" for information on usage and redistribution
# of this file, and for a DISCLAIMER OF ALL WARRANTIES.

namespace import -force tcltest::testConstraint

#
# WINDOWING SYSTEM AND DISPLAY
#
testConstraint notAqua [expr {[tk windowingsystem] ne "aqua"}]
testConstraint aqua [expr {[tk windowingsystem] eq "aqua"}]
testConstraint x11 [expr {[tk windowingsystem] eq "x11"}]
testConstraint nonwin [expr {[tk windowingsystem] ne "win32"}]
testConstraint aquaOrWin32 [expr {
    ([tk windowingsystem] eq "win32") || [testConstraint aqua]
}]
testConstraint haveDISPLAY [expr {[info exists env(DISPLAY)] && [testConstraint x11]}]
testConstraint altDisplay  [info exists env(TK_ALT_DISPLAY)]

# constraint for running a test on all windowing system except aqua
# where the test fails due to a known bug
testConstraint aquaKnownBug [expr {[testConstraint notAqua] || [testConstraint knownBug]}]

# constraint based on whether our display is secure
testutils import child
childTkProcess create
set app [childTkProcess eval {tk appname}]
testConstraint secureserver 0
if {[llength [info commands send]]} {
    testConstraint secureserver 1
    if {[catch {send $app set a 0} msg] == 1} {
	if {[string match "X server insecure *" $msg]} {
	    testConstraint secureserver 0
	}
    }
}
childTkProcess exit
testutils forget child

testConstraint failsOnUbuntu [expr {![info exists ::env(CI)] || ![string match Linux $::tcl_platform(os)]}]
testConstraint failsOnXQuartz [expr {$tcl_platform(os) ne "Darwin" || [tk windowingsystem] ne "x11" }]

#
# FONTS
#
testConstraint fonts 1
destroy .e
entry .e -width 0 -font {Helvetica -12} -bd 1 -highlightthickness 1
.e insert end a.bcd
if {([winfo reqwidth .e] != 37) || ([winfo reqheight .e] != 20)} {
    testConstraint fonts 0
}
destroy .e
destroy .t
text .t -width 80 -height 20 -font {Times -14} -bd 1
pack .t
.t insert end "This is\na dot."
update
set x [list [.t bbox 1.3] [.t bbox 2.5]]
destroy .t
if {![string match {{22 3 6 15} {31 18 [34] 15}} $x]} {
    testConstraint fonts 0
}

testConstraint withXft [expr {![catch {tk::pkgconfig get fontsystem} fs] && ($fs eq "xft")}]
testConstraint withoutXft [expr {![testConstraint withXft]}]
unset fs

# Expected results of some tests on Linux rely on availability of the "times"
# font. This font is generally provided when Tk uses the old X font system,
# but not when using Xft on top of fontconfig. Specifically (old system):
#    xlsfonts | grep times
# may return quite some output while (new system):
#    fc-list | grep times
# return value is empty. That's not surprising since the two font systems are
# separate (availability of a font in one of them does not mean it's available
# in the other one). The following constraints are useful in this kind of
# situation.
testConstraint haveTimesFamilyFont [expr {
    [string tolower [font actual {-family times} -family]] eq "times"
}]
testConstraint haveFixedFamilyFont [expr {
    [string tolower [font actual {-family fixed} -family]] eq "fixed"
}]

# Although unexpected, some systems may have a very limited set of fonts available.
# The following constraints happen to evaluate to false at least on one system: the
# Github CI runner for Linux with --disable-xft, which has exactly ONE single font
# ([font families] returns a single element: "fixed"), for which [font actual]
# returns:
#    -family fixed -size 9 -weight normal -slant roman -underline 0
# and [font metrics] returns:
#    -ascent 11 -descent 2 -linespace 13 -fixed 1
# The following constraints are hence tailored to check exactly what is needed in the
# tests they constrain (that is: availability of any font having the given font
# attributes), so that these constrained tests will in fact run on all systems having
# reasonable font dotation.
testConstraint havePointsize37Font [expr {
    [font actual {-family courier -size 37} -size] == 37
}]
testConstraint havePointsize14BoldFont [expr {
    ([font actual {times 14 bold} -size] == 14) &&
    ([font actual {times 14 bold} -weight] eq "bold")
}]
testConstraint haveBoldItalicUnderlineOverstrikeFont [expr {
    ([font actual {times 12 bold italic overstrike underline} -weight] eq "bold") &&
    ([font actual {times 12 bold italic overstrike underline} -slant] eq "italic") &&
    ([font actual {times 12 bold italic overstrike underline} -underline] eq "1") &&
    ([font actual {times 12 bold italic overstrike underline} -overstrike] eq "1")
}]
set fixedFont {Courier 12}   ; # warning: must be consistent with the files using the constraint below!
set bigFont   {Helvetica 24} ; # ditto
testConstraint haveBigFontTwiceLargerThanTextFont [expr {
    [font actual $fixedFont -size] * 2 <= [font actual $bigFont -size]
}]
unset fixedFont bigFont

#
# VISUALS
#
testConstraint pseudocolor8 [expr {
    ([catch {
	toplevel .t -visual {pseudocolor 8} -colormap new
    }] == 0) && ([winfo depth .t] == 8)
}]
destroy .t
testConstraint haveTruecolor24 [expr {
    {truecolor 24} in [winfo visualsavailable .]
}]
testConstraint haveGrayscale8 [expr {
    {grayscale 8} in [winfo visualsavailable .]
}]
testConstraint defaultPseudocolor8 [expr {
    ([winfo visual .] eq "pseudocolor") && ([winfo depth .] == 8)
}]


#
# VARIOUS
#
testConstraint userInteraction 0
testConstraint nonUnixUserInteraction [expr {
    [testConstraint userInteraction] ||
    ([testConstraint unix] && [testConstraint notAqua])
}]

testConstraint deprecated [expr {![::tk::build-info no-deprecate]}]

# constraints for testing facilities defined in the tktest executable
testConstraint testbitmap      [llength [info commands testbitmap]]
testConstraint testborder      [llength [info commands testborder]]
testConstraint testcbind       [llength [info commands testcbind]]
testConstraint testclipboard   [llength [info commands testclipboard]]
testConstraint testcolor       [llength [info commands testcolor]]
testConstraint testcursor      [llength [info commands testcursor]]
testConstraint testembed       [llength [info commands testembed]]
testConstraint testfont        [llength [info commands testfont]]
testConstraint testImageType   [expr {"test" in [image types]}]
testConstraint testmakeexist   [llength [info commands testmakeexist]]
testConstraint testmenubar     [llength [info commands testmenubar]]
testConstraint testmetrics     [llength [info commands testmetrics]]
testConstraint testmovemouse   [llength [info commands testmovemouse]]
testConstraint testobjconfig   [llength [info commands testobjconfig]]
testConstraint testpressbutton [llength [info commands testpressbutton]]
testConstraint testsend        [llength [info commands testsend]]
testConstraint testtext        [llength [info commands testtext]]
testConstraint testwinevent    [llength [info commands testwinevent]]
testConstraint testwrapper     [llength [info commands testwrapper]]

# EOF
