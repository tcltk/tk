#biditext.tcl
#
# Demonstration of bidirectional text support in Tk.
# Features a text widget with three paragraphs illustrating LTR, RTL,
# and mixed bidirectional content, plus a canvas with three rotated words.

package require tk

# --- Main window ---
set w .biditext
catch {destroy $w}
toplevel $w
wm title $w "Bidirectional Text Demonstration"
wm iconname $w "biditext"
positionWindow $w

label $w.msg -font $font -wraplength 4i -anchor w -justify left \
	-text  "These are examples of bidirectional text in Tk." 
pack $w.msg -side top -pady 10

## See Code / Dismiss buttons
set btns [addSeeDismiss $w.buttons $w]
pack $btns -side bottom -fill x

# --- Frame to hold text and canvas side by side ---
set main $w.main
frame $main
pack $main -expand yes -fill both -padx 10 -pady 5


# ----- Text widget (three paragraphs) -----
frame $main.textFrame -relief sunken -bd 1
pack $main.textFrame -fill both -expand yes
text $main.textFrame.txt \
    -yscrollcommand [list $main.textFrame.scroll set] \
    -wrap word -width 70 -height 15 -undo 1 -font TkDefaultFont
ttk::scrollbar $main.textFrame.scroll -command [list $main.textFrame.txt yview]
pack $main.textFrame.scroll -side right -fill y
pack $main.textFrame.txt -expand yes -fill both

# Insert the three paragraphs (first paragraph includes an intro sentence)
$main.textFrame.txt insert end \
"1. Left‑to‑right (LTR) paragraph:\n\
   English text flows from left to right. This is the default direction for many languages.\n\n"

$main.textFrame.txt insert end \
"2. Right‑to‑left (RTL) paragraph:\n\
   هذا النص باللغة العربية. النص يسير من اليمين إلى اليسار.\n\
   Tk يعرضه بشكل صحيح مع دعم ثنائي الاتجاه.\n\n"

$main.textFrame.txt insert end \
"3. Mixed bidirectional paragraph:\n\
   The price is 100 USD (LTR) while السعر هو ١٠٠ دولار (RTL).\n\
   Numbers and punctuation are handled according to Unicode bidi rules."

$main.textFrame.txt configure -state normal
$main.textFrame.txt mark set insert 0.0
focus $main.textFrame.txt

# ----- Canvas widget with three words rotated 45 degrees -----
canvas $main.canvas -width 240 -height 200 -bg white -relief sunken -bd 1
pack $main.canvas -side right -fill both -expand yes -padx 10

# Create a font of size 14 for the canvas
catch {font create BidiCanvasFont -size 14}

# Three words (LTR, RTL, neutral) placed with 45° rotation
set words { "Hello" "مرحبا" "Tk" }
set yPos { 50 100 150 }
foreach word $words y $yPos {
    $main.canvas create text 70 $y -text $word \
        -font BidiCanvasFont -angle 45 -anchor center \
        -fill navy -tags "rotatedWord"
}

# Aadd a subtle border label for the canvas
$main.canvas create text 120 185 -text "(rotated 45°)" \
    -font {TkDefaultFont 9} -fill gray60 -anchor center


# --- Final focus ---
focus $main.textFrame.txt
