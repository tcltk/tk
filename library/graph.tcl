# graph.tcl --
#
# This file defines the default bindings for Tk graph widgets and provides
# procedures that help in implementing those bindings.
#

## Used namespace.
namespace eval ::graph {
    variable _zoom
    array set _zoom {}
}

## ::graph binding --
#
#   Enable and disable default bindings for specified elements.
#
# Arguments:
# args -    list of entries for which to enable default bindings.
#           For not given entries bindings will be disabled.
#           Possible names are:
#               legend, crosshairs, zoom, findelement
#
::oo::define ::graph method bindings {args} {
    array set myArgs {legend 0 crosshairs 0 zoom 0 findelement 0}
    foreach myName $args {
        if {![info exists myArgs($myName)]} {
            return -code error "wrong or double name '$myName' in given list"
        }
        set myArgs($myName) 1
    }
    set my  [namespace which my]
    if {$myArgs(legend)} {
        my legend bind all <Enter> [list $my _legend activate]
	    my legend bind all <Leave> [list $my _legend deactivate]
	    my legend bind all <ButtonPress-1> [list $my _legend select]
    } else {
        my legend bind all <Enter> {}
	    my legend bind all <Leave> {}
	    my legend bind all <ButtonPress-1> {}
    }
    if {$myArgs(crosshairs)} {
        my crosshairs on
        my _bindtag bind cross <Any-Motion> {%W crosshairs configure -position @%x,%y}
        my _bindtag bind cross <Leave> {%W crosshairs off}
        my _bindtag bind cross <Enter> {%W crosshairs on}
	    my _bindtag add cross
    } else {
        my crosshairs off
	    my _bindtag remove cross
    }
    if {$myArgs(zoom)} {
        my _zoom init
        my _bindtag bind zoom <ButtonPress-1> [list $my _zoom setpoint %x %y]
        my _bindtag bind zoom <ButtonPress-3> [list $my _zoom reset]
        my _bindtag add zoom
    } else {
        my _zoom remove
	    my _bindtag remove zoom
    }
    if {$myArgs(findelement)} {
        my _bindtag bind find <Control-ButtonPress-2> [list $my _findelement %x %y]
        my _bindtag bind find <Control-ButtonRelease-2> [list $my _findelement {} {}]
        my _bindtag add find
    } else {
        my _findelement {} {}
        my _bindtag remove find
    }
}

#
# Private functions.
#

# ::graph _legend --
#
#   Helper function for legend bindings.
#
# Arguments:
# mode -    Indicate what to do
#   - activate "w"
#   - deactivate "w"
#   - select "w"
::oo::define ::graph method _legend {mode} {
    set elem [my legend get current]
    switch -- $mode {
        activate {
            my legend activate $elem
        }
        deactivate {
            my legend deactivate $elem
        }
        select {
            set relief [my element cget $elem -labelrelief]
            if { $relief eq "flat" } {
        	    my element configure $elem -labelrelief raised
        	    my element activate $elem
            } else {
        	    my element configure $elem -labelrelief flat
        	    my element deactivate $elem
            }
        }
    }
}

# ::graph _bindtag --
#
#   Helper function to add/remove of bindtags.
#
# Arguments:
# mode -    Indicate what to do
#   - add "widget" "tag"
#   - remove "widget" "tag"
# tag -     Tag to add or remove
::oo::define ::graph method _bindtag {mode tag args} {
    set w   [string range [self] 2 end]
    set oldTagList [bindtags $w]
    append tag $w;#add window path name to make it unique
    switch -- $mode {
        add {
            if {$tag ni $oldTagList} {
	            bindtags $w [linsert $oldTagList 0 $tag]
            }
        }
        remove {
            set index [lsearch $oldTagList $tag]
            if { $index >= 0 } {
	            bindtags $w [lreplace $oldTagList $index $index]
            }
        }
        bind {
            puts "bind $tag {*}$args"
            bind $tag {*}$args
        }
    }
}

option add *_graph_findelelementText.font	  -*-helvetica-medium-R-*-*-10-*-*-*-*-*-*-* 

# ::graph _findelement --
#
#   Helper function to flash nearest elemnt.
#
# Arguments:
# graph -   Name of graph widget
# x -       x coordinate
# y -       y coordinate
::oo::define ::graph method _findelement {x y} {
	my marker delete _graph_findelementText _graph_findelementLine
    if {$x eq {} || $y eq {} || ![my element closest $x $y my -halo 50]} {
	    return
    }
    # my(name)		- element Id
    # my(index)		- index of closest point
    # my(x) my(y)	- coordinates of closest point
    # my(dist)		- distance from sample coordinate
    lassign [my invtransform $x $y] x y
    if {$x > $my(x)} {
        if {$y > $my(y)} {
            set myAnchor sw
        } else {
            set myAnchor nw
        }
    } else {
        if {$y > $my(y)} {
            set myAnchor se
        } else {
            set myAnchor ne
        }
    }
    my marker create text \
        -coords [list $x $y] \
	    -name   _graph_findelementText \
	    -text   "$my(name)($my(index))=$my(x),$my(y)" \
	    -anchor $myAnchor\
        -bg     {} 
    my marker create line\
        -coords     [list $x $y $my(x) $my(y)]\
        -linewidth  2\
	    -name _graph_findelementLine

    my element activate $my(name) $my(index)
    after 2000 [self] element deactivate $my(name)
}

option add *_graph_zoomOutline.dashes		4	
option add *_graph_zoomTitle.anchor		    nw
option add *_graph_zoomOutline.lineWidth	2
option add *_graph_zoomOutline.xor		    yes
option add *_graph_zoomTitle.shadow	        yellow4
option add *_graph_zoomTitle.foreground     yellow1
option add *_graph_zoomTitle.coords	        [list -Inf Inf]

# ::graph _zoom --
#
#   Helper function for zooming.
#
# Arguments:
# mode    - indicate what to do
#   - init
#   - destroy
#   - titledestroy
#   - titlenext
#   - titlelast
#   - pop
#   - push
#   - reset
#   - setpoint "x" "y"
#   - box "x" "y"
#   - marchingants
# w -   Name of graph widget
#       
::oo::define ::graph method _zoom {mode args} {
    variable _zoom

    switch -- $mode {
    init {;# initialize zooming data
        set _zoom(interval) 100
        set _zoom(afterId) 0
        set _zoom(A,x) {}
        set _zoom(A,y) {}
        set _zoom(B,x) {}
        set _zoom(B,y) {}
        set _zoom(stack) {}
        set _zoom(corner) A
        set _zoom(ants) 0
    }
    titledestroy {
        if { $_zoom(corner) eq "A" } {
	        catch { my marker delete "_graph_zoomTitle" }
        }
    }
    titlenext {
        set level [expr {[llength $_zoom(stack)] + 1}]
        if { [my cget -invertxy] } {
	        set coords [list -Inf -Inf]
        } else {
	        set coords [list -Inf Inf]
        }
        my marker create text\
        -name       "_graph_zoomTitle"\
        -text       "Zoom #$level"\
	    -coords     $coords\
        -bindtags   ""\
        -anchor     nw

    }
    titlelast {
        set level [llength $_zoom(stack)]
        if { $level > 0 } {
     	    my marker create text\
            -name   "_graph_zoomTitle"\
            -anchor nw\
	        -text   "Zoom #$level" 
        }
    }
    pop {
        if { [llength $_zoom(stack)] > 0 } {
	        set _zoom(stack) [lassign $_zoom(stack) cmd]
	        uplevel #0 $cmd
	        my _zoom titlelast
            # get window from self
	        event generate [string range [self] 2 end] <Configure>
	        update idletasks
	        after 2000 [list [namespace which my] _zoom titledestroy]
        } else {
	        catch { my marker delete "_graph_zoomTitle" }
        }
    }
    push {
        my marker delete {*}[my marker names "_graph_zoom*"]
        if { [info exists _zoom(afterId)] } {
	        after cancel $_zoom(afterId)
        }
        set x1 $_zoom(A,x)
        set y1 $_zoom(A,y)
        set x2 $_zoom(B,x)
        set y2 $_zoom(B,y)

	    # No delta, revert to start
        if { ($x1 == $x2) || ($y1 == $y2) } return
        set cmd {}
        foreach margin { xaxis yaxis x2axis y2axis } {
	        foreach axis [my $margin use] {
	            set min [my axis cget $axis -min] 
	            set max [my axis cget $axis -max]
	            append cmd [list [self] axis configure $axis -min $min -max $max] \
		        "\n"
	        }
        }
        set _zoom(stack) [linsert $_zoom(stack) 0 $cmd]

        foreach margin { xaxis x2axis } {
	        foreach axis [my $margin use] {
	            set min [my axis invtransform $axis $x1]
	            set max [my axis invtransform $axis $x2]
	            if { $min > $max } { 
		            my axis configure $axis -min $max -max $min
	            } else {
		            my axis configure $axis -min $min -max $max
	            }
	        }
        }
        foreach margin { yaxis y2axis } {
	        foreach axis [my $margin use] {
	            set min [my axis invtransform $axis $y1]
	            set max [my axis invtransform $axis $y2]
	            if { $min > $max } { 
		            my axis configure $axis -min $max -max $min
	            } else {
		            my axis configure $axis -min $min -max $max
	            }
	        }
        }
        # get window from self
        event generate [string range [self] 2 end] <Configure>
        update idletasks
    }
    reset {
        # This routine terminates either an existing zoom, or pops back to
        # the previous zoom level (if no zoom is in progress).
        if { ![info exists _zoom(corner)] } {
	        my _zoom init
        }
        my marker delete {*}[my marker names "_graph_zoom*"]

        if { $_zoom(corner) eq "A" } {
	        # Reset the whole axis
	        my _zoom pop
        } else {
	        set _zoom(corner) A
	        my _bindtag remove select-region
        }
    }
    remove {;# reset to very first level
        if {[info exists _zoom(stack)]} {
            if { [llength $_zoom(stack)] > 0 } {
	            uplevel #0 [lindex $_zoom(stack) 0]
	            event generate [string range [self] 2 end] <Configure>
            }
            my marker delete {*}[my marker names "_graph_zoom*"]
        }
        my _zoom init
    }
    setpoint {;# 
        lassign $args x y
        if { ![info exists _zoom(corner)] } {
		    my _zoom init
        }
        my _bindtag bind zoomregion <Any-Motion>\
            [list [namespace which my] _zoom box %x %y]
        if { $_zoom(corner) eq {A} } {
		    if { ![my inside $x $y] } return
            if { [my cget -invertxy] } {
		        set _zoom(A,x) $y
		        set _zoom(A,y) $x
            } else {
		        set _zoom(A,x) $x
		        set _zoom(A,y) $y
            }
		    # First corner selected, start watching motion events
		    my _zoom titlenext 
		    my _bindtag add zoomregion
		    set _zoom(corner) B
        } else {
            if { [my cget -invertxy] } {
		        set _zoom(B,x) $y
		        set _zoom(B,y) $x
            } else {
		        set _zoom(B,x) $x
		        set _zoom(B,y) $y
            }
		    # Delete the modal binding
		    my _bindtag remove zoomregion
		    my _zoom push
		    set _zoom(corner) A
        }
    }
    box {;#
        lassign $args x y;# coords of second point
        if { [my cget -invertxy] } {
	        set _zoom(B,x) $y
	        set _zoom(B,y) $x
        } else {
	        set _zoom(B,x) $x
	        set _zoom(B,y) $y
        }
        if { $_zoom(A,x) > $_zoom(B,x) } { 
	        set x1 [my xaxis invtransform $_zoom(B,x)]
	        set y1 [my yaxis invtransform $_zoom(B,y)]
	        set x2 [my xaxis invtransform $_zoom(A,x)]
	        set y2 [my yaxis invtransform $_zoom(A,y)]
        } else {
	        set x1 [my xaxis invtransform $_zoom(A,x)]
	        set y1 [my yaxis invtransform $_zoom(A,y)]
	        set x2 [my xaxis invtransform $_zoom(B,x)]
	        set y2 [my yaxis invtransform $_zoom(B,y)]
        }
        set coords { $x1 $y1 $x2 $y1 $x2 $y2 $x1 $y2 $x1 $y1 }
        if { [my marker exists "_graph_zoomOutline"] } {
	        my marker configure "_graph_zoomOutline" -coords $coords 
        } else {
	        set X [lindex [my xaxis use] 0]
	        set Y [lindex [my yaxis use] 0]
	        my marker create line\
            -coords $coords\
            -name   "_graph_zoomOutline"\
	        -mapx   $X\
            -mapy   $Y
	        set interval $_zoom(interval)
            set _zoom(ants) 0
	        set id [after $interval [list [namespace which my] _zoom marchingants]]
	        set _zoom(afterId) $id
        }
    }
    marchingants {
        incr _zoom(ants)
        if {[my marker exists _graph_zoomOutline]} {
	        my marker configure _graph_zoomOutline -dashoffset $_zoom(ants)
	        set interval $_zoom(interval)
	        set id [after $interval [list [namespace which my] _zoom marchingants]]
	        set _zoom(afterId) $id
        }
    }
    default {}
    }
}

# TODO Check if ::tk::ps_preamble can be used instead.
# It is used be the graph postscript method.
set ::::graph::ps_preamble\
{%%BeginProlog
%
% PostScript prolog file of the rbc graph widget.
%
% Copyright 1989-1992 Regents of the University of California.
% Permission to use, copy, modify, and distribute this
% software and its documentation for any purpose and without
% fee is hereby granted, provided that the above copyright
% notice appear in all copies.  The University of California
% makes no representations about the suitability of this
% software for any purpose.  It is provided "as is" without
% express or implied warranty.
%
% Copyright 1991-1997 Bell Labs Innovations for Lucent Technologies.
%
% Permission to use, copy, modify, and distribute this software and its
% documentation for any purpose and without fee is hereby granted, provided
% that the above copyright notice appear in all copies and that both that the
% copyright notice and warranty disclaimer appear in supporting documentation,
% and that the names of Lucent Technologies any of their entities not be used
% in advertising or publicity pertaining to distribution of the software
% without specific, written prior permission.
%
% Lucent Technologies disclaims all warranties with regard to this software,
% including all implied warranties of merchantability and fitness.  In no event
% shall Lucent Technologies be liable for any special, indirect or
% consequential damages or any damages whatsoever resulting from loss of use,
% data or profits, whether in an action of contract, negligence or other
% tortuous action, arising out of or in connection with the use or performance
% of this software.
%

200 dict begin

/BaseRatio 1.3467736870885982 def	% Ratio triangle base / symbol size
/BgColorProc 0 def			% Background color routine (symbols)
/DrawSymbolProc 0 def			% Routine to draw symbol outline/fill
/StippleProc 0 def			% Stipple routine (bar segments)
/DashesProc 0 def			% Dashes routine (line segments)
  
% Define the array ISOLatin1Encoding (which specifies how characters are 
% encoded for ISO-8859-1 fonts), if it isn't already present (Postscript 
% level 2 is supposed to define it, but level 1 doesn't). 
 
systemdict /ISOLatin1Encoding known not { 
  /ISOLatin1Encoding [ 
    /space /space /space /space /space /space /space /space 
    /space /space /space /space /space /space /space /space 
    /space /space /space /space /space /space /space /space 
    /space /space /space /space /space /space /space /space 
    /space /exclam /quotedbl /numbersign /dollar /percent /ampersand 
    /quoteright 
    /parenleft /parenright /asterisk /plus /comma /minus /period /slash 
    /zero /one /two /three /four /five /six /seven 
    /eight /nine /colon /semicolon /less /equal /greater /question 
    /at /A /B /C /D /E /F /G 
    /H /I /J /K /L /M /N /O 
    /P /Q /R /S /T /U /V /W 
    /X /Y /Z /bracketleft /backslash /bracketright /asciicircum /underscore 
    /quoteleft /a /b /c /d /e /f /g 
    /h /i /j /k /l /m /n /o 
    /p /q /r /s /t /u /v /w 
    /x /y /z /braceleft /bar /braceright /asciitilde /space 
    /space /space /space /space /space /space /space /space 
    /space /space /space /space /space /space /space /space 
    /dotlessi /grave /acute /circumflex /tilde /macron /breve /dotaccent 
    /dieresis /space /ring /cedilla /space /hungarumlaut /ogonek /caron 
    /space /exclamdown /cent /sterling /currency /yen /brokenbar /section 
    /dieresis /copyright /ordfeminine /guillemotleft /logicalnot /hyphen 
    /registered /macron 
    /degree /plusminus /twosuperior /threesuperior /acute /mu /paragraph 
    /periodcentered 
    /cedillar /onesuperior /ordmasculine /guillemotright /onequarter 
    /onehalf /threequarters /questiondown 
    /Agrave /Aacute /Acircumflex /Atilde /Adieresis /Aring /AE /Ccedilla 
    /Egrave /Eacute /Ecircumflex /Edieresis /Igrave /Iacute /Icircumflex 
    /Idieresis 
    /Eth /Ntilde /Ograve /Oacute /Ocircumflex /Otilde /Odieresis /multiply 
    /Oslash /Ugrave /Uacute /Ucircumflex /Udieresis /Yacute /Thorn 
    /germandbls 
    /agrave /aacute /acircumflex /atilde /adieresis /aring /ae /ccedilla 
    /egrave /eacute /ecircumflex /edieresis /igrave /iacute /icircumflex 
    /idieresis 
    /eth /ntilde /ograve /oacute /ocircumflex /otilde /odieresis /divide 
    /oslash /ugrave /uacute /ucircumflex /udieresis /yacute /thorn 
    /ydieresis 
  ] def 
} if 

% font ISOEncode font 
% This procedure changes the encoding of a font from the default 
% Postscript encoding to ISOLatin1.  It is typically invoked just 
% before invoking "setfont".  The body of this procedure comes from 
% Section 5.6.1 of the Postscript book. 

/ISOEncode { 
  dup length dict
  begin 
    {1 index /FID ne {def} {pop pop} ifelse} forall 
    /Encoding ISOLatin1Encoding def 
    currentdict 
  end 

  % I'm not sure why it's necessary to use "definefont" on this new 
  % font, but it seems to be important; just use the name "Temporary" 
  % for the font. 

  /Temporary exch definefont 
} bind def 

/Stroke {
  gsave
    stroke
  grestore
} def

/Fill {
  gsave
    fill
  grestore
} def

/SetFont { 	
  % Stack: pointSize fontName
  findfont exch scalefont ISOEncode setfont
} def

/Box {
  % Stack: x y width height
  newpath
  exch 4 2 roll moveto
  dup 0 rlineto
  exch 0 exch rlineto
  neg 0 rlineto
  closepath
} def

/SetFgColor {
  % Stack: red green blue
  CL 0 eq { 
    pop pop pop 0 0 0 
  } if
  setrgbcolor
  CL 1 eq { 
    currentgray setgray 
  } if
} def

/SetBgColor {
  % Stack: red green blue
  CL 0 eq { 
    pop pop pop 1 1 1 
  } if
  setrgbcolor
  CL 1 eq { 
    currentgray setgray 
  } if
} def

% The next two definitions are taken from "$tk_library/prolog.ps"

% desiredSize EvenPixels closestSize
%
% The procedure below is used for stippling.  Given the optimal size
% of a dot in a stipple pattern in the current user coordinate system,
% compute the closest size that is an exact multiple of the device's
% pixel size.  This allows stipple patterns to be displayed without
% aliasing effects.

/EvenPixels {
  % Compute exact number of device pixels per stipple dot.
  dup 0 matrix currentmatrix dtransform
  dup mul exch dup mul add sqrt

  % Round to an integer, make sure the number is at least 1, and compute
  % user coord distance corresponding to this.
  dup round dup 1 lt {pop 1} if
  exch div mul
} bind def

% width height string filled StippleFill --
%
% Given a path and other graphics information already set up, this
% procedure will fill the current path in a stippled fashion.  "String"
% contains a proper image description of the stipple pattern and
% "width" and "height" give its dimensions.  If "filled" is true then
% it means that the area to be stippled is gotten by filling the
% current path (e.g. the interior of a polygon); if it's false, the
% area is gotten by stroking the current path (e.g. a wide line).
% Each stipple dot is assumed to be about one unit across in the
% current user coordinate system.

% width height string StippleFill --
%
% Given a path already set up and a clipping region generated from
% it, this procedure will fill the clipping region with a stipple
% pattern.  "String" contains a proper image description of the
% stipple pattern and "width" and "height" give its dimensions.  Each
% stipple dot is assumed to be about one unit across in the current
% user coordinate system.  This procedure trashes the graphics state.

/StippleFill {
    % The following code is needed to work around a NeWSprint bug.

    /tmpstip 1 index def

    % Change the scaling so that one user unit in user coordinates
    % corresponds to the size of one stipple dot.
    1 EvenPixels dup scale

    % Compute the bounding box occupied by the path (which is now
    % the clipping region), and round the lower coordinates down
    % to the nearest starting point for the stipple pattern.  Be
    % careful about negative numbers, since the rounding works
    % differently on them.

    pathbbox
    4 2 roll
    5 index div dup 0 lt {1 sub} if cvi 5 index mul 4 1 roll
    6 index div dup 0 lt {1 sub} if cvi 6 index mul 3 2 roll

    % Stack now: width height string y1 y2 x1 x2
    % Below is a doubly-nested for loop to iterate across this area
    % in units of the stipple pattern size, going up columns then
    % across rows, blasting out a stipple-pattern-sized rectangle at
    % each position

    6 index exch {
	2 index 5 index 3 index {
	    % Stack now: width height string y1 y2 x y

	    gsave
	    1 index exch translate
	    5 index 5 index true matrix tmpstip imagemask
	    grestore
	} for
	pop
    } for
    pop pop pop pop pop
} bind def


/LS {	% Stack: x1 y1 x2 y2
  newpath 4 2 roll moveto lineto stroke
} def

/EndText {
  %Stack :
  grestore
} def

/BeginText {
  %Stack :  w h theta centerX centerY
  gsave
    % Translate the origin to the center of bounding box and rotate
    translate neg rotate
    % Translate back to the origin of the text region
    -0.5 mul exch -0.5 mul exch translate
} def

/DrawAdjText {
  %Stack : str strWidth x y
  moveto				% Go to the text position
  exch dup dup 4 2 roll

  % Adjust character widths to get desired overall string width
  % adjust X = (desired width - real width)/#chars

  stringwidth pop sub exch
  length div
  0 3 -1 roll

  % Flip back the scale so that the string is not drawn in reverse

  gsave
    1 -1 scale
    ashow
  grestore
} def

/DrawBitmap {
  % Stack: ?bgColorProc? boolean centerX centerY width height theta imageStr
  gsave
    6 -2 roll translate			% Translate to center of bounding box
    4 1 roll neg rotate			% Rotate by theta
    
    % Find upperleft corner of bounding box
    
    2 copy -.5 mul exch -.5 mul exch translate
    2 copy scale			% Make pixel unit scale
    newpath
    0 0 moveto 0 1 lineto 1 1 lineto 1 0 lineto
    closepath
    
    % Fill rectangle with background color
    
    4 -1 roll { 
      gsave 
	4 -1 roll exec fill 
      grestore 
    } if
    
    % Paint the image string into the unit rectangle
    
    2 copy true 3 -1 roll 0 0 5 -1 roll 0 0 6 array astore 5 -1 roll
    imagemask
  grestore
} def

% Symbols:

% Skinny-cross
/Sc {
  % Stack: x y symbolSize
  gsave
    3 -2 roll translate 45 rotate
    0 0 3 -1 roll Sp
  grestore
} def

% Skinny-plus
/Sp {
  % Stack: x y symbolSize
  gsave
    3 -2 roll translate
    2 idiv
    dup 2 copy
    newpath neg 0 moveto 0 lineto
    DrawSymbolProc
    newpath neg 0 exch moveto 0 exch lineto
    DrawSymbolProc
  grestore
} def

% Cross
/Cr {
  % Stack: x y symbolSize
  gsave
    3 -2 roll translate 45 rotate
    0 0 3 -1 roll Pl
  grestore
} def

% Plus
/Pl {
  % Stack: x y symbolSize
  gsave
    3 -2 roll translate
    dup 2 idiv
    exch 6 idiv

    %
    %          2   3		The plus/cross symbol is a
    %				closed polygon of 12 points.
    %      0   1   4    5	The diagram to the left
    %           x,y		represents the positions of
    %     11  10   7    6	the points which are computed
    %				below.
    %          9   8
    %

    newpath
    2 copy exch neg exch neg moveto dup neg dup lineto
    2 copy neg exch neg lineto 2 copy exch neg lineto
    dup dup neg lineto 2 copy neg lineto 2 copy lineto
    dup dup lineto 2 copy exch lineto 2 copy neg exch lineto
    dup dup neg exch lineto exch neg exch lineto
    closepath
    DrawSymbolProc
  grestore
} def

% Circle
/Ci {
  % Stack: x y symbolSize
  gsave
    3 copy pop
    moveto newpath
    2 div 0 360 arc
    closepath DrawSymbolProc
  grestore
} def

% Square
/Sq {
  % Stack: x y symbolSize
  gsave
    dup dup 2 div dup
    6 -1 roll exch sub exch
    5 -1 roll exch sub 4 -2 roll Box
    DrawSymbolProc
  grestore
} def

% Line
/Li {
  % Stack: x y symbolSize
  gsave
    3 1 roll exch 3 -1 roll 2 div 3 copy
    newpath
    sub exch moveto add exch lineto
    stroke
  grestore
} def

% Diamond
/Di {
  % Stack: x y symbolSize
  gsave
    3 1 roll translate 45 rotate 0 0 3 -1 roll Sq
  grestore
} def
    
% Triangle
/Tr {
  % Stack: x y symbolSize
  gsave
    3 -2 roll translate
    BaseRatio mul 0.5 mul		% Calculate 1/2 base
    dup 0 exch 30 cos mul		% h1 = height above center point
    neg					% b2 0 -h1
    newpath moveto			% point 1;  b2
    dup 30 sin 30 cos div mul		% h2 = height below center point
    2 copy lineto			% point 2;  b2 h2
    exch neg exch lineto		% 
    closepath
    DrawSymbolProc
  grestore
} def

% Arrow
/Ar {
  % Stack: x y symbolSize
  gsave
    3 -2 roll translate
    BaseRatio mul 0.5 mul		% Calculate 1/2 base
    dup 0 exch 30 cos mul		% h1 = height above center point
					% b2 0 h1
    newpath moveto			% point 1;  b2
    dup 30 sin 30 cos div mul		% h2 = height below center point
    neg					% -h2 b2
    2 copy lineto			% point 2;  b2 h2
    exch neg exch lineto		% 
    closepath
    DrawSymbolProc
  grestore
} def

% Bitmap
/Bm {
  % Stack: x y symbolSize
  gsave
    3 1 roll translate pop DrawSymbolProc
  grestore
} def

%%EndProlog

%%BeginSetup
gsave					% Save the graphics state

% Default line/text style parameters

1 setlinewidth				% width
1 setlinejoin				% join
0 setlinecap				% cap
[] 0 setdash				% dashes

/CL 0 def				% Set color level mode
0 0 0 setrgbcolor			% color
}
# vim: set ts=4 sw=4 sts=4 ff=unix et :
