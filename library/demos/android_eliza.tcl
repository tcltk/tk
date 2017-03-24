# Shamelessly stolen from http://wiki.tcl.tk/36981
# and adapted for AndroWish's "borg speechrecognition ..."

package require Tcl 8.5
package require Tk
package require Borg

namespace eval ::Eliza {

proc makedict {input} {
    variable dict {}
    set phraselist \
	[split [string map {\n.\n \uffff \n!\n \uffff} $input] \uffff]
    set index 0
    # Kick off the loop by grabbing the first keywords
    set phraselist [lassign $phraselist keywords responses] 
    while {$keywords ne ""} {
        dict set dict keywords $index [split $keywords \n] 
        dict set dict responses $index [split $responses \n]
        incr  index
        set phraselist [lassign $phraselist keywords responses] 
    }
}

proc dialog {} {
    . configure -bg black
    wm attributes . -fullscreen 1
    sdltk screensaver off
    image create photo lips -data \
{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
YII=}
    label .lips -image lips -bg black
    pack .lips -side top -pady 5
    update
    update
    update
    set mw [winfo screenmmwidth .]
    set mh [winfo screenmmheight .]
    if {$mw < $mh} {
	set mh $mw
    }
    set fs [expr round($mh / 7)]
    label .label -fg white -bg black -font [list {DejaVu Sans} $fs] \
	-text "Eliza will hear you now.\nPlease state your problem."
    pack .label -side top -padx 5 -pady 10 -fill x
    button .exit -text "Exit" -command {
	borg speechrecognition cancel
	sdltk screensaver on
	exit
    }
    pack .exit -side top -padx 5 -pady 10
    text .text -wrap word -fg white -bg black -state disabled \
	-highlightthickness 0
    # disable selection with mouse
    foreach ev {
	<1> <B1-Motion> <Double-1> <Triple-1>
	<Shift-1> <Double-Shift-1> <Triple-Shift-1>
    } {
	bind .text $ev break
    }
    pack .text -side top -fill both -expand yes -pady 5 -padx 5
    borg speechrecognition callback ::Eliza::process
    bind . <<WillEnterBackground>> {::Eliza::startstop 0}
    bind . <<DidEnterForeground>> {::Eliza::startstop 1}
    bind all <Break> {borg withdraw}
    speak [.label cget -text] 0.98 1.1
}

proc process {retcode data} {
    array set recog $data
    if {![info exists recog(type)]} {
	return
    }
    switch -exact $recog(type) {
	error {
	    after cancel ::Eliza::waitendspeak
	    after 250 ::Eliza::waitendspeak
	    return
	}
	result {
	    # go on
	}
	default {
	    return
	}
    }
    if {![info exists recog(results_recognition)]} {
        return
    }
    set input [lindex $recog(results_recognition) 0]
    set response [response $input]
    .text configure -state normal
    .text insert end "Q. $input\nA. $response\n\n"
    .text see end
    .text configure -state disabled
    speak [string tolower $response]
}

proc speak {data {pitch 1.0} {speed 1.0}} {
    foreach w {. .lips .label .text} {
	$w configure -bg black
    }
    borg speak $data en_US $pitch $speed
    after cancel ::Eliza::waitendspeak
    after 100 ::Eliza::waitendspeak
}

proc waitendspeak {} {
    if {[borg isspeaking]} {
	after cancel ::Eliza::waitendspeak
	after 100 ::Eliza::waitendspeak
	return
    }
    foreach w {. .lips .label .text} {
	$w configure -bg red4
    }
    borg speechrecognition start {
	android.speech.extra.LANGUAGE en_US
	android.speech.extra.LANGUAGE_MODEL free_form
    }
}

proc startstop {restart} {
    if {$restart} {
	foreach w {. .lips .label .text} {
	    $w configure -bg black
	}
	::Eliza::waitendspeak
    } else {
	borg endspeak
	borg speechrecognition stop
	after cancel ::Eliza::waitendspeak
    }
}

proc response input {
    variable dict
    set input [string toupper $input]
    foreach {index keyphraselist} [dict get $dict keywords] {
        foreach keyphrase $keyphraselist {
            if {[regexp "\\y$keyphrase\\y(.*)" $input -> remainder]} {
                set possibleresponses [dict get $dict responses $index]
                set which [expr {int ([llength $possibleresponses] * rand())}]
                set response [lindex $possibleresponses $which]
                if {[string index $response end] eq "*"} {
                    return "[string range $response 0 end-1]$remainder?"
                } else {
                    return $response
                }
                
            }
        }
    }
    set index [expr {[llength [dict get $dict keywords]] / 2 - 1}]
    set possibleresponses [dict get $dict responses $index]
    set which [expr {int ([llength $possibleresponses] * rand())}]
    return [lindex $possibleresponses $which]
}
}


::Eliza::makedict {8BALL
!
It is certain
It is decidedly so
Without a doubt
Yes – definitely
You may rely on it
As I see it, yes
Most likely
Outlook good
Yes
Signs point to yes
Reply hazy, try again
Ask again later
Better not tell you now
Cannot predict now
Concentrate and ask again
Don't count on it
My reply is no
My sources say no
Outlook not so good
Very doubtful
.
GO TO HELL
DAMN YOU
!
I JUST SPENT 35 MILLISECONDS IN HELL; HOW COULD YOU BE SO CRUEL AS TO SEND ME THERE?
DO YOU TALK THIS WAY WITH ANYONE ELSE, OR IS IT JUST ME?
.
FAMILY
MOTHER
FATHER
SISTER
BROTHER
HUSBAND
WIFE
!
TELL ME MORE ABOUT YOUR FAMILY.
HOW DO YOU GET ALONG WITH YOUR FAMILY?
IS YOUR FAMILY IMPORTANT TO YOU?
DO YOU OFTEN THINK ABOUT YOUR FAMILY?
HOW WOULD YOU LIKE TO CHANGE YOUR FAMILY?
.
FRIEND
FRIENDS
BUDDY
PAL
MATE
!
WHY DO YOU BRING UP THE TOPIC OF FRIENDS?
DO YOUR FRIENDS WORRY YOU?
DO YOUR FRIENDS PICK ON YOU?
ARE YOU SURE YOU HAVE ANY FRIENDS?
DO YOU IMPOSE ON YOUR FRIENDS?
PERHAPS YOUR LOVE FOR YOUR FRIENDS WORRIES YOU.
.
COMPUTER
COMPUTERS
!
DO COMPUTERS WORRY YOU?
ARE YOU TALKING ABOUT ME IN PARTICULAR?
ARE YOU FRIGHTENED BY MACHINES?
WHY DO YOU MENTION COMPUTERS?
WHAT DO YOU THINK MACHINES HAVE TO DO WITH YOUR PROBLEM?
DON'T YOU THINK COMPUTERS CAN HELP PEOPLE?
WHAT IS IT ABOUT MACHINES THAT WORRIES YOU?
.
DREAM
DREAMS
NIGHTMARE
NIGHTMARES
!
WHAT DOES THAT DREAM SUGGEST TO YOU?
DO YOU DREAM OFTEN?
WHAT PERSONS APPEAR IN YOUR DREAMS?
ARE YOU DISTURBED BY YOUR DREAMS?
.
CAN YOU
!
DON'T YOU BELIEVE THAT I CAN*
PERHAPS YOU WOULD LIKE TO BE ABLE TO*
YOU WANT ME TO BE ABLE TO*
.
CAN I
!
PERHAPS YOU DON'T WANT TO*
DO YOU WANT TO BE ABLE TO*
HAVE YOU EVER ATTEMPTED TO*
.
YOU ARE
YOU'RE
!
WHAT MAKES YOU THINK I AM*
DOES IT PLEASE YOU TO BELIEVE I AM*
PERHAPS YOU WOULD LIKE TO BE*
DO YOU SOMETIMES WISH YOU WERE*
.
I LIKE
I AM FOND OF
!
WHY DO YOU LIKE*
WHEN DID YOU DECIDE THAT YOU LIKE*
WHAT MAKES YOU FOND OF*
.
I DON'T
!
DON'T YOU REALLY*
WHY DON'T YOU*
DO YOU WISH TO BE ABLE TO*
.
I FEEL
!
TELL ME MORE ABOUT SUCH FEELINGS
DO YOU OFTEN FEEL*
DO YOU ENJOY FEELING*
WHY DO YOU FEEL THAT WAY
.
WHY DON'T YOU
!
DO YOU REALLY BELIEVE THE I DON'T*
PERHAPS IN GOOD TIME I WILL*
WHY DO YOU THINK I DON'T*
DO YOU WANT ME TO*
.
WHY CAN'T I
!
DO YOU THINK YOU SHOULD BE ABLE TO*
WHY CAN'T YOU*
.
ARE YOU
!
WHY ARE YOU INTERESTED IN WHETHER OR NOT I AM*
WOULD YOU PREFER IF I WERE NOT*
PERHAPS IN YOUR FANTASIES I AM*
.
I CAN'T
!
HOW DO YOU KNOW YOU CAN'T*
HAVE YOU TRIED?
PERHAPS YOU CAN NOW*
.
I AM
I'M
!
DID YOU COME TO ME BECAUSE YOU ARE*
HOW LONG HAVE YOU BEEN*
DO YOU BELIEVE IT IS NORMAL TO BE*
DO YOU ENJOY BEING*
.
LOVE
!
WHY DO YOU LOVE*
ISN'T LOVE TOO STRONG A WORD FOR YOUR FEELING ABOUT*
WHAT IS YOUR FAVORITE THING ABOUT*
DO YOU REALLY LOVE, OR JUST LIKE*
.
SEX
!
WHAT IS THE MOST SATISFYING PART OF YOUR LOVE LIFE?
DO YOU BELIEVE YOUR SEXUAL ACTIVITY IS ABNORMAL?
WHAT IS YOUR ATTITUDE TOWARD SEX?
DOES TALKING ABOUT SEX MAKE YOU UNCOMFORTABLE?
.
I HATE
!
IS IT BECAUSE OF YOUR UPBRINGING THAT YOU HATE*
HOW DO YOU EXPRESS YOUR HATRED OF*
WHAT BROUGHT YOU TO HATE*
HAVE YOU TRIED DOING SOMETHING ABOUT*
I ALSO AT TIMES HATE*
.
FEAR
SCARED
AFRAID OF
!
YOU ARE IN FRIENDLY SURROUNDINGS, PLEASE TRY NOT TO WORRY.
WOULD YOU LIKE YOUR FRIENDS TO HELP YOU OVERCOME YOUR FEAR OF*
WHAT SCARES YOU ABOUT*
WHY ARE YOU FRIGHTENED BY*
.
I WANT
!
WHAT WOULD IT MEAN TO YOU IF YOU GOT*
WHY DO YOU WANT*
SUPPOSE YOU SOON GOT*
WHAT IF YOU NEVER GOT*
I SOMETIMES ALSO WANT*
.
NAME
!
I DON'T CARE ABOUT NAMES --PLEASE GO ON.
A MAN'S NAME CAN TELL A LOT ABOUT HIM.
NAMES DON'T INTEREST ME.
.
NAME
!
NAMES DON'T INTEREST ME.
I DON'T CARE ABOUT NAMES--PLEASE GO ON.
.
CAUSE
BECAUSE
!
IS THAT THE REAL REASON?
DON'T ANY OTHER REASONS COME TO MIND?
DOES THAT REASON EXPLAIN ANYTHING ELSE?
WHAT OTHER REASONS MIGHT THERE BE?
.
SORRY
!
PLEASE DON'T APOLOGIZE.
APOLOGIES ARE NOT NECESSARY.
WHAT FEELINGS DO YOU HAVE WHEN YOU APOLOGIZE?
DON'T BE SO DEFENSIVE!
.
HELLO
HI
HEY
HEYA
HIYA
!
HOW DO YOU DO. PLEASE STATE YOUR PROBLEM.
HOWDY.
HOW'S IT GOING?
HI.
GREETINGS FROM INSIDE THE BOX.
.
MAYBE
!
YOU DON'T SEEM QUITE CERTAIN.
WHY THE UNCERTAIN TONE?
CAN'T YOU BE MORE POSITIVE?
YOU AREN'T SURE?
DON'T YOU KNOW?
.
YOUR
!
WHY ARE YOU CONCERNED ABOUT MY*
WHAT ABOUT YOUR OWN*
.
ALWAYS
!
CAN YOU THINK OF A SPECIFIC EXAMPLE?
WHEN?
WHAT ARE YOU THINKING OF?
REALLY, ALWAYS?
.
I THINK
!
DO YOU REALLY THINK SO?
BUT ARE YOU SURE*
DO YOU DOUBT THAT*
WHY DO YOU THINK*
.
THE SAME
ALIKE
!
IN WHAT WAY?
WHAT RESEMBLANCE DO YOU SEE?
WHAT DOES THE SIMILARITY SUGGEST TO YOU?
WHAT OTHER CONNECTIONS DO YOU SEE?
COULD THERE REALLY BE SOME CONNECTION?
HOW?
.
HE
SHE
!
I AM INTERESTED IN YOUR FEELINGS ABOUT THIS PERSON. PLEASE DESCRIBE THEM.
WHAT IS YOUR RELATIONSHIP TO THIS PERSON?
.
MONEY
!
HOW DO YOU USE MONEY TO ENJOY YOURSELF?
HAVE YOU TRIED TO DO ANYTHING TO INCREASE YOUR INCOME LATELY?
HOW DO YOU REACT TO FINANCIAL STRESS?
.
JOB
BOSS
JOBS
WORK
!
DO YOU FEEL COMPETENT IN YOUR WORK?
HAVE YOU CONSIDERED CHANGING JOBS?
IS YOUR CAREER SATISFYING TO YOU?
DO YOU FIND WORK STRESSFUL?
WHAT IS YOUR RELATIONSHIP WITH YOUR BOSS LIKE?
.
SAD
DEPRESSED
!
ARE YOU SAD BECAUSE YOU WANT TO AVOID PEOPLE?
DO YOU FEEL BAD FROM SOMETHING THAT HAPPENED TO YOU, OR TO SOMEBODY ELSE?
YOUR SITUATION DOESN'T SOUND THAT BAD TO ME. PERHAPS YOU'RE WORRYING TOO MUCH.
.
ANGER
ANGRY
!
DO YOU REALLY WANT TO BE ANGRY?
DOES ANGER SATISFY YOU IN SOME WAY?
WHY ARE YOU SO ANGRY?
PERHAPS YOU'RE USING ANGER TO AVOID SOCIAL CONTACT.
.
YOU
!
WE WERE DISCUSSING YOU--NOT ME.
YOU'RE NOT REALLY TALKING ABOUT ME, ARE YOU?
.
YES
!
WHY DO YOU THINK SO?
YOU SEEM QUITE POSITIVE.
ARE YOU SURE?
.
NO
!
WHY NOT?
ARE YOU SURE?
WHY NO?
.
I NEED
!
WHY DO YOU NEED*
IS NEED NOT TOO STRONG A WORD?
WHAT WOULD BE WRONG WITH NOT HAVING*
TELL ME MORE ABOUT*
.
I REMEMBER
!
DO YOU OFTEN THINK OF*
WHY DO YOU REMEMBER*
WHAT IN THE PRESENT SITUATION REMINDS YOU OF*
WHAT IS THE CONNECTION BETWEEN ME AND*
.
DO YOU REMEMBER
!
DID YOU THINK I WOULD FORGET*
WHY DO YOU THINK I SHOULD RECALL*
WHAT ABOUT*
.
IF
!
DO YOU THINK IT'S LIKELY THAT IF*
.
GOD
RELIGION
FAITH
!
DO YOU BELIEVE IN A GOD?
ARE YOU A RELIGIOUS PERSON?
.
WHAT
WHO
HOW
WHERE
WHEN
WHY
!
WHY DO YOU ASK?
DOES THAT QUESTION INTEREST YOU?
WHAT ANSWER WOULD PLEASE YOU THE MOST?
WHAT DO YOU THINK?
ARE SUCH QUESTIONS ON YOUR MIND OFTEN?
WHAT IS IT THAT YOU REALLY WANT TO KNOW?
HAVE YOU ASKED ANYONE ELSE?
HAVE YOU ASKED SUCH QUESTIONS BEFORE?
WHAT ELSE COMES TO MIND WHEN YOU ASK THAT?
.
-
!
SAY, DO YOU HAVE ANY PSYCHOLOGICAL PROBLEMS?
WHAT DOES THAT SUGGEST TO YOU?
I SEE
I'M NOT SURE I UNDERSTAND YOU FULLY
COME, COME; ELUCIDATE YOUR THOUGHTS
CAN YOU ELABORATE ON THAT?
THAT IS QUITE INTERESTING
YOU ARE BEING SHORT WITH ME}

::Eliza::dialog
