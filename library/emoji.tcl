# emoji.tcl -- Tk emoji chooser for Linux (X11 and Wayland)
#
# A pure Tk dialog; no native code needed.
#  - Uses color emoji fonts when available (Noto Color Emoji, Apple Color
#    Emoji, Segoe UI Emoji, Twemoji), falling back to monochrome fonts.
#  - Bound to Control-. / Control-; and Super-. / Super-; (Mod4) on text,
#    entry and ttk::entry
#  - Popup over the widget, insert at insertion cursor
#  - Announces emoji to a running screen reader via tk::accessible::speak
#    on X11, using hardcoded CLDR TTS annotation names.
#
# Does nothing on Windows and macOS, which have their own system pickers.

package require Tk 9.0-

namespace eval ::tk::emoji {
	
	# Bail if X11 and compiled without Xft support
	if {[tk::build-info no-xft]} {
	    return
	}

    variable S
    array set S {
        top    .tkemojipicker
        cell   28
        cat    ""
        cur    -1
        cols   1
        items  {}
        drawnW -1
    }
    variable recent {}
    variable maxRecent 24

    variable categories {
        Recent Smileys Gestures People Animals Food Travel Activities
        Objects Symbols
    }
    variable icons [dict create \
			Recent \u263A Smileys \U1F603 Gestures \u270B People \u263A \
			Animals \U1F43E Food \u2615 Travel \u2708 Activities \u26BD \
			Objects \u2692 Symbols \u2665]

    # Full emoji set including Unicode 12/13/14+ additions that only
    # exist in color emoji fonts. Every code point here should render
    # via Noto Color Emoji, Apple Color Emoji, Segoe UI Emoji, or
    # Twemoji on a modern system.
    variable data [dict create \
		       Smileys {
			   \u263A \u263B \U1F600 \U1F603 \U1F604 \U1F601 \U1F606 \U1F605 \U1F602 \U1F923
			   \U1F642 \U1F643 \U1F609 \U1F60A \U1F607 \U1F970 \U1F60D \U1F929 \U1F618 \U1F617
			   \U1F61A \U1F619 \U1F972 \U1F60B \U1F61B \U1F61C \U1F92A \U1F61D \U1F911 \U1F917
			   \U1F92D \U1F92B \U1F914 \U1F910 \U1F928 \U1F610 \U1F611 \U1F636 \U1F60F \U1F612
			   \U1F644 \U1F62C \U1F925 \U1F60C \U1F614 \U1F62A \U1F924 \U1F634 \U1F637 \U1F912
			   \U1F915 \U1F922 \U1F92E \U1F927 \U1F975 \U1F976 \U1F974 \U1F635 \U1F92F \U1F920
			   \U1F973 \U1F978 \U1F60E \U1F913 \U1F9D0 \U1F615 \U1F61F \U1F641 \U1F62E \U1F62F
			   \U1F632 \U1F633 \U1F97A \U1F626 \U1F627 \U1F628 \U1F630 \U1F625 \U1F622 \U1F62D
			   \U1F631 \U1F616 \U1F623 \U1F61E \U1F613 \U1F629 \U1F62B \U1F971 \U1F624 \U1F621
			   \U1F620 \U1F92C \U1F608 \U1F47F \U1F480 \u2620 \U1F4A9 \U1F921 \U1F479 \U1F47A
			   \U1F47B \U1F47D \U1F47E \U1F916 \U1F63A \U1F638 \U1F639 \U1F63B \U1F63C \U1F63D
			   \U1F640 \U1F63F \U1F63E
		       } \
		       Gestures {
			   \U1F44B \U1F91A \U1F590 \u270B \U1F596 \U1F44C \U1F90C \U1F90F \u270C \U1F91E
			   \U1F91F \U1F918 \U1F919 \U1F448 \U1F449 \U1F446 \U1F595 \U1F447 \u261D \U1F446
			   \U1F44A \U1F44D \U1F44E \u270A \U1F91B \U1F91C \U1F44F \U1F64C \U1F450 \U1F932
			   \U1F91D \U1F64F \u270D \U1F485 \U1F933 \U1F4AA \U1F9BE \U1F9BF \U1F9B5 \U1F9B6
			   \U1F442 \U1F9BB \U1F443 \U1F9E0 \U1F9B7 \U1F9B4 \U1F445 \U1F444
		       } \
		       People {
			   \U1F476 \U1F9D2 \U1F466 \U1F467 \U1F9D1 \U1F471 \U1F468 \U1F9D4 \U1F469
			   \U1F474 \U1F475 \U1F64D \U1F64E \U1F645 \U1F646 \U1F481 \U1F64B \U1F9CF
			   \U1F647 \U1F926 \U1F937 \U1F46E \U1F575 \U1F482 \U1F977 \U1F477 \U1F934
			   \U1F478 \U1F473 \U1F472 \U1F9D5 \U1F935 \U1F470 \U1F930 \U1F931 \U1F47C \U1F385
			   \U1F936 \U1F9B8 \U1F9B9 \U1F9D9 \U1F9DA \U1F9DB \U1F9DC \U1F9DD \U1F9DE \U1F9DF
			   \U1F486 \U1F487 \U1F6B6 \U1F9CD \U1F9CE \U1F3C3 \U1F483 \U1F57A \U1F574 \U1F46F
			   \U1F9D6 \U1F9D7 \U1F93A \U1F3C7 \U1F9D8 \U1F46D \U1F46B \U1F46C \U1F48F \U1F491
			   \U1F46A
		       } \
		       Animals {
			   \U1F435 \U1F412 \U1F98D \U1F9A7 \U1F436 \U1F415 \U1F9AE \U1F429 \U1F9A9 \U1F43A
			   \U1F98A \U1F99D \U1F431 \U1F408 \U1F981 \U1F42F \U1F405 \U1F406 \U1F434 \U1F40E
			   \U1F984 \U1F993 \U1F98C \U1F9AE \U1F42E \U1F402 \U1F403 \U1F404 \U1F437 \U1F416
			   \U1F417 \U1F43D \U1F40F \U1F411 \U1F410 \U1F42A \U1F42B \U1F999 \U1F992 \U1F418
			   \U1F98F \U1F99B \U1F42D \U1F439 \U1F430 \U1F407 \U1F99A \U1F99C \U1F438 \U1F40A
			   \U1F422 \U1F98E \U1F40D \U1F432 \U1F409 \U1F995 \U1F996 \U1F433 \U1F40B \U1F42C
			   \U1F41F \U1F420 \U1F421 \U1F988 \U1F419 \U1F41A \U1F980 \U1F99E \U1F990 \U1F991
			   \U1F9AA \U1F40C \U1F98B \U1F41B \U1F41C \U1F41D \U1F41E \U1F997 \U1F577 \U1F578
			   \U1F982 \U1F99F \U1F9A0 \U1F9A1 \U1F9A2 \U1F9A3 \U1F9A4 \U1F9A5 \U1F9A6 \U1F9A8
			   \U1F9AB \U1F9AC \U1F9AD \U1F42C \U1F41F \U1F43E
		       } \
		       Food {
			   \U1F347 \U1F348 \U1F349 \U1F34A \U1F34B \U1F34C \U1F34D \U1F96D \U1F34E \U1F34F
			   \U1F350 \U1F351 \U1F352 \U1F353 \U1F95D \U1F345 \U1F965 \U1F951 \U1F346 \U1F954
			   \U1F955 \U1F33D \U1F336 \U1F33C \U1F96C \U1F966 \U1F9C4 \U1F9C5 \U1F344 \U1F95C
			   \U1F330 \U1F35E \U1F950 \U1F956 \U1F968 \U1F9C0 \U1F95A \U1F373 \U1F9C8 \U1F95E
			   \U1F9C7 \U1F953 \U1F969 \U1F357 \U1F356 \U1F32D \U1F354 \U1F35F \U1F355 \U1F32E
			   \U1F32F \U1F959 \U1F9C6 \U1F95D \U1F96A \U1F9C3 \U1F958 \U1F35D \U1F96B \U1F35C
			   \U1F372 \U1F963 \U1F957 \U1F37F \U1F9C2 \U1F96F \U1F95F \U1F960 \U1F961 \U1F980
			   \U1F365 \U1F363 \U1F364 \U1F35E \U1F359 \U1F35A \U1F358 \U1F362 \U1F361 \U1F367
			   \U1F368 \U1F366 \U1F967 \U1F370 \U1F382 \U1F9C1 \U1F36E \U1F36D \U1F36C \U1F36B
			   \U1F37F \U1F369 \U1F36A \U1F36F \U1F37C \u2615 \U1F375 \U1F376 \U1F37E \U1F377
			   \U1F378 \U1F379 \U1F37A \U1F37B \U1F942 \U1F943 \U1F964 \U1F9CB \U1F9C3
		       } \
		       Travel {
			   \U1F697 \U1F695 \U1F699 \U1F68C \U1F68E \U1F3CE \U1F693 \U1F691 \U1F692 \U1F690
			   \U1F69A \U1F69B \U1F69C \U1F3CE \U1F6B2 \U1F6F4 \U1F6F5 \U1F6F9 \U1F68F \U1F6A8
			   \U1F694 \U1F68D \U1F698 \U1F68B \U1F682 \U1F683 \U1F684 \U1F685 \U1F686 \U1F687
			   \U1F688 \U1F689 \U1F68A \U1F69D \U1F69E \U1F68B \U1F683 \u2708 \U1F6EB \U1F6EC
			   \U1F6E9 \U1F4BA \U1F680 \U1F6F8 \U1F6F6 \u26F5 \U1F6A4 \U1F6F3 \u26F4 \U1F6E5
			   \U1F6A2 \u2693 \u26FD \U1F6A7 \U1F6A6 \U1F6A5 \U1F68F \U1F5FA \U1F5FF \U1F3D4
			   \U1F30B \U1F5FB \U1F3D5 \U1F3D6 \U1F3DC \U1F3DD \U1F3DE \U1F3DF \U1F3E0 \U1F3E1
			   \U1F3D8 \U1F3DA \U1F3D7 \U1F3ED \U1F3E2 \U1F3EC \U1F3E3 \U1F3E4 \U1F3E5 \U1F3E6
			   \U1F3E8 \U1F3EA \U1F3EB \U1F3E9 \U1F492 \U1F3EF \U1F3F0 \U1F3A1 \U1F3A2 \U1F3A0
			   \u26F2 \u26F0 \U1F30B
		       } \
		       Activities {
			   \u26BD \U1F3C0 \U1F3C8 \u26BE \U1F94E \U1F3BE \U1F3D0 \U1F3C9 \U1F94F \U1F3B1
			   \U1F3D3 \U1F3F8 \U1F3D2 \U1F3D1 \U1F94D \U1F3CF \U1F3D2 \U1F94A \U1F94B \U1F945
			   \u26F3 \u26F8 \U1F3A3 \U1F93F \U1F3BD \U1F3BF \U1F6F7 \U1F94C \U1F3C2 \U1F3CB
			   \U1F6B4 \U1F6B5 \U1F938 \U1F93C \U1F93D \U1F93E \U1F939 \U1F9D8 \U1F3CC \U1F3C4
			   \U1F6A3 \U1F3CA \u26F9 \U1F3CB \U1F6B5 \U1F6B4 \U1F3C6 \U1F947 \U1F948 \U1F949
			   \U1F3C5 \U1F396 \U1F3F5 \U1F397 \U1F39F \U1F3AB \U1F3AA \U1F3AD \U1F3A8 \U1F3AC
			   \U1F3A4 \U1F3A7 \U1F3BC \U1F3B9 \U1F941 \U1F3B7 \U1F3BA \U1F3B8 \U1F3BB \U1F3B2
			   \u265F \U1F3AF \U1F3B3 \U1F3AE \U1F3B0 \U1F9E9
		       } \
		       Objects {
			   \u231A \U1F4F1 \U1F4F2 \U1F4BB \u2328 \U1F5A5 \U1F5A8 \U1F5B1 \U1F5B2 \U1F579
			   \U1F5DC \U1F4BD \U1F4BE \U1F4BF \U1F4C0 \U1F4FC \U1F4F7 \U1F4F8 \U1F4F9 \U1F3A5
			   \U1F4FD \U1F39E \U1F4DE \u260E \U1F4DF \U1F4E0 \U1F4FA \U1F4FB \U1F399 \U1F39A
			   \U1F39B \u23F1 \u23F2 \u23F0 \U1F570 \u231B \u23F3 \U1F4E1 \U1F50B \U1F50C
			   \U1F4A1 \U1F526 \U1F56F \U1F6E2 \U1F4B8 \U1F4B5 \U1F4B4 \U1F4B6 \U1F4B7 \U1F4B0
			   \U1F4B3 \U1F48E \u2696 \U1F9F0 \U1F527 \U1F528 \u2692 \U1F6E0 \u26CF \U1F529
			   \u2699 \U1F5DC \u26D3 \U1F9F1 \U1F4A3 \U1F52A \U1F5E1 \u2694 \U1F6E1 \U1F6AC
			   \u26B0 \u26B1 \U1F3FA \U1F52E \U1F52D \U1F52C \U1F573 \U1F9F2 \U1F9F3 \U1F6BD
			   \U1F9F4 \U1F9F5 \U1F9F6 \U1F9F7 \U1F9F8 \U1F9F9 \U1F9FA \U1F9FB \U1F9FC \U1F9FD
			   \U1F9FE \U1F9FF
		       } \
		       Symbols {
			   \u2764 \U1F9E1 \U1F49B \U1F49A \U1F499 \U1F49C \U1F5A4 \U1F90D \U1F90E \U1F494
			   \u2763 \U1F495 \U1F49E \U1F493 \U1F497 \U1F496 \U1F498 \U1F49D \U1F49F \u262E
			   \u271D \u262A \u2626 \u271F \u262F \u262A \u2626 \u26CE \u2648 \u2649 \u264A
			   \u264B \u264C \u264D \u264E \u264F \u2650 \u2651 \u2652 \u2653 \U1F194 \u269B
			   \U1F251 \u2622 \u2623 \U1F4F4 \U1F4F3 \U1F236 \U1F21A \U1F238 \U1F23A \U1F237
			   \u2734 \U1F19A \U1F4AE \U1F250 \u3299 \u3297 \U1F234 \U1F235 \U1F239 \U1F232
			   \U1F170 \U1F171 \U1F18E \U1F191 \U1F17E \U1F198 \u274C \u2B55 \U1F6D1 \u26D4
			   \U1F4DB \U1F6AB \U1F4AF \U1F4A2 \u2668 \U1F6B7 \U1F6AF \U1F6B3 \U1F6B1 \U1F51E
			   \U1F4F5 \U1F6AD \u2757 \u2755 \u2753 \u2754 \u203C \u2049 \u303D \u26A0
			   \U1F6B8 \U1F531 \u269C
		       }]

    # CLDR TTS annotation names, hardcoded. Keyed by the emoji character.
    # Names are the canonical "text to speech" strings from CLDR's
    # common/annotations/en.xml (type="tts"), lowercased.
    variable names [dict create \
        \u263A  "smiling face" \
        \u263B  "smiling face with open mouth" \
        \u2620  "skull and crossbones" \
        \u270B  "raised hand" \
        \u270C  "victory hand" \
        \u270A  "raised fist" \
        \u270D  "writing hand" \
        \u2615  "hot beverage" \
        \u2708  "airplane" \
        \u26F5  "sailboat" \
        \u2693  "anchor" \
        \u26FD  "fuel pump" \
        \u26F2  "fountain" \
        \u26F0  "mountain" \
        \u26BD  "soccer ball" \
        \u26BE  "baseball" \
        \u26F3  "flag in hole" \
        \u26F8  "ice skate" \
        \u26F7  "skier" \
        \u26F9  "person bouncing ball" \
        \u265F  "chess pawn" \
        \u231A  "watch" \
        \u2328  "keyboard" \
        \u260E  "telephone" \
        \u23F1  "stopwatch" \
        \u23F2  "timer clock" \
        \u23F0  "alarm clock" \
        \u231B  "hourglass done" \
        \u23F3  "hourglass not done" \
        \u2696  "balance scale" \
        \u2692  "hammer and pick" \
        \u26CF  "pick" \
        \u2699  "gear" \
        \u26D3  "chains" \
        \u2694  "crossed swords" \
        \u26B0  "coffin" \
        \u26B1  "funeral urn" \
        \u2764  "red heart" \
        \u2763  "heart with arrow" \
        \u262E  "peace symbol" \
        \u271D  "latin cross" \
        \u262A  "star and crescent" \
        \u2626  "orthodox cross" \
        \u271F  "star of david" \
        \u262F  "yin yang" \
        \u26CE  "ophiuchus" \
        \u2648  "aries" \
        \u2649  "taurus" \
        \u264A  "gemini" \
        \u264B  "cancer" \
        \u264C  "leo" \
        \u264D  "virgo" \
        \u264E  "libra" \
        \u264F  "scorpio" \
        \u2650  "sagittarius" \
        \u2651  "capricorn" \
        \u2652  "aquarius" \
        \u2653  "pisces" \
        \u269B  "atom symbol" \
        \u2622  "radioactive" \
        \u2623  "biohazard" \
        \u2734  "eight pointed star" \
        \u3299  "japanese secret button" \
        \u3297  "japanese congratulations button" \
        \u274C  "cross mark" \
        \u2B55  "hollow red circle" \
        \u26D4  "no entry" \
        \u2668  "hot springs" \
        \u2757  "exclamation mark" \
        \u2755  "white exclamation mark" \
        \u2753  "question mark" \
        \u2754  "white question mark" \
        \u203C  "double exclamation mark" \
        \u2049  "exclamation question mark" \
        \u303D  "part alternation mark" \
        \u26A0  "warning" \
        \u269C  "fleur de lis" \
        \U1F600 "grinning face" \
        \U1F603 "grinning face with big eyes" \
        \U1F604 "grinning face with smiling eyes" \
        \U1F601 "beaming face with smiling eyes" \
        \U1F606 "grinning squinting face" \
        \U1F605 "grinning face with sweat" \
        \U1F602 "face with tears of joy" \
        \U1F923 "rolling on the floor laughing" \
        \U1F642 "slightly smiling face" \
        \U1F643 "upside down face" \
        \U1F609 "winking face" \
        \U1F60A "smiling face with smiling eyes" \
        \U1F607 "smiling face with halo" \
        \U1F970 "smiling face with hearts" \
        \U1F60D "smiling face with heart eyes" \
        \U1F929 "star struck" \
        \U1F618 "face blowing a kiss" \
        \U1F617 "kissing face" \
        \U1F61A "kissing face with closed eyes" \
        \U1F619 "kissing face with smiling eyes" \
        \U1F972 "smiling face with tear" \
        \U1F60B "face savoring food" \
        \U1F61B "face with tongue" \
        \U1F61C "winking face with tongue" \
        \U1F92A "zany face" \
        \U1F61D "squinting face with tongue" \
        \U1F911 "money mouth face" \
        \U1F917 "hugging face" \
        \U1F92D "face with hand over mouth" \
        \U1F92B "shushing face" \
        \U1F914 "thinking face" \
        \U1F910 "zipper mouth face" \
        \U1F928 "face with raised eyebrow" \
        \U1F610 "neutral face" \
        \U1F611 "expressionless face" \
        \U1F636 "face without mouth" \
        \U1F60F "smirking face" \
        \U1F612 "unamused face" \
        \U1F644 "face with rolling eyes" \
        \U1F62C "grimacing face" \
        \U1F925 "lying face" \
        \U1F60C "relieved face" \
        \U1F614 "pensive face" \
        \U1F62A "sleepy face" \
        \U1F924 "drooling face" \
        \U1F634 "sleeping face" \
        \U1F637 "face with medical mask" \
        \U1F912 "face with thermometer" \
        \U1F915 "face with head bandage" \
        \U1F922 "nauseated face" \
        \U1F92E "face vomiting" \
        \U1F927 "sneezing face" \
        \U1F975 "hot face" \
        \U1F976 "cold face" \
        \U1F974 "woozy face" \
        \U1F635 "face with crossed out eyes" \
        \U1F92F "exploding head" \
        \U1F920 "cowboy hat face" \
        \U1F973 "partying face" \
        \U1F978 "disguised face" \
        \U1F60E "smiling face with sunglasses" \
        \U1F913 "nerd face" \
        \U1F9D0 "face with monocle" \
        \U1F615 "confused face" \
        \U1F61F "worried face" \
        \U1F641 "slightly frowning face" \
        \U1F62E "face with open mouth" \
        \U1F62F "hushed face" \
        \U1F632 "astonished face" \
        \U1F633 "flushed face" \
        \U1F97A "pleading face" \
        \U1F626 "frowning face with open mouth" \
        \U1F627 "anguished face" \
        \U1F628 "fearful face" \
        \U1F630 "anxious face with sweat" \
        \U1F625 "sad but relieved face" \
        \U1F622 "crying face" \
        \U1F62D "loudly crying face" \
        \U1F631 "face screaming in fear" \
        \U1F616 "confounded face" \
        \U1F623 "persevering face" \
        \U1F61E "disappointed face" \
        \U1F613 "downcast face with sweat" \
        \U1F629 "weary face" \
        \U1F62B "tired face" \
        \U1F971 "yawning face" \
        \U1F624 "face with steam from nose" \
        \U1F621 "enraged face" \
        \U1F620 "angry face" \
        \U1F92C "face with symbols on mouth" \
        \U1F608 "smiling face with horns" \
        \U1F47F "angry face with horns" \
        \U1F480 "skull" \
        \U1F4A9 "pile of poo" \
        \U1F921 "clown face" \
        \U1F479 "ogre" \
        \U1F47A "goblin" \
        \U1F47B "ghost" \
        \U1F47D "alien" \
        \U1F47E "alien monster" \
        \U1F916 "robot" \
        \U1F63A "grinning cat" \
        \U1F638 "grinning cat with smiling eyes" \
        \U1F639 "cat with tears of joy" \
        \U1F63B "smiling cat with heart eyes" \
        \U1F63C "cat with wry smile" \
        \U1F63D "kissing cat" \
        \U1F640 "weary cat" \
        \U1F63F "crying cat" \
        \U1F63E "pouting cat" \
        \U1F44B "waving hand" \
        \U1F91A "raised back of hand" \
        \U1F590 "hand with fingers splayed" \
        \U1F596 "vulcan salute" \
        \U1F44C "ok hand" \
        \U1F90C "pinched fingers" \
        \U1F90F "pinching hand" \
        \U1F91E "crossed fingers" \
        \U1F91F "love you gesture" \
        \U1F918 "sign of the horns" \
        \U1F919 "call me hand" \
        \U1F448 "backhand index pointing left" \
        \U1F449 "backhand index pointing right" \
        \U1F446 "backhand index pointing up" \
        \U1F595 "middle finger" \
        \U1F447 "backhand index pointing down" \
        \u261D "index pointing up" \
        \U1F44A "oncoming fist" \
        \U1F44D "thumbs up" \
        \U1F44E "thumbs down" \
        \U1F91B "left facing fist" \
        \U1F91C "right facing fist" \
        \U1F44F "clapping hands" \
        \U1F64C "raising hands" \
        \U1F450 "open hands" \
        \U1F932 "palms up together" \
        \U1F91D "handshake" \
        \U1F64F "folded hands" \
        \U1F485 "nail polish" \
        \U1F933 "selfie" \
        \U1F4AA "flexed biceps" \
        \U1F9BE "mechanical arm" \
        \U1F9BF "mechanical leg" \
        \U1F9B5 "leg" \
        \U1F9B6 "foot" \
        \U1F442 "ear" \
        \U1F9BB "ear with hearing aid" \
        \U1F443 "nose" \
        \U1F9E0 "brain" \
        \U1F9B7 "tooth" \
        \U1F9B4 "bone" \
        \U1F445 "tongue" \
        \U1F444 "mouth" \
        \U1F476 "baby" \
        \U1F9D2 "child" \
        \U1F466 "boy" \
        \U1F467 "girl" \
        \U1F9D1 "person" \
        \U1F471 "person blond hair" \
        \U1F468 "man" \
        \U1F9D4 "man beard" \
        \U1F469 "woman" \
        \U1F474 "old man" \
        \U1F475 "old woman" \
        \U1F64D "person frowning" \
        \U1F64E "person pouting" \
        \U1F645 "person gesturing no" \
        \U1F646 "person gesturing ok" \
        \U1F481 "person tipping hand" \
        \U1F64B "person raising hand" \
        \U1F9CF "deaf person" \
        \U1F647 "person bowing" \
        \U1F926 "person facepalming" \
        \U1F937 "person shrugging" \
        \U1F46E "police officer" \
        \U1F575 "detective" \
        \U1F482 "guard" \
        \U1F977 "ninja" \
        \U1F477 "construction worker" \
        \U1F934 "prince" \
        \U1F478 "princess" \
        \U1F473 "person wearing turban" \
        \U1F472 "person with skullcap" \
        \U1F9D5 "person with headscarf" \
        \U1F935 "person in tuxedo" \
        \U1F470 "person with veil" \
        \U1F930 "pregnant woman" \
        \U1F931 "breast feeding" \
        \U1F47C "baby angel" \
        \U1F385 "santa claus" \
        \U1F936 "mrs claus" \
        \U1F9B8 "superhero" \
        \U1F9B9 "supervillain" \
        \U1F9D9 "mage" \
        \U1F9DA "fairy" \
        \U1F9DB "vampire" \
        \U1F9DC "merperson" \
        \U1F9DD "elf" \
        \U1F9DE "genie" \
        \U1F9DF "zombie" \
        \U1F486 "person getting massage" \
        \U1F487 "person getting haircut" \
        \U1F6B6 "person walking" \
        \U1F9CD "person standing" \
        \U1F9CE "person kneeling" \
        \U1F3C3 "person running" \
        \U1F483 "woman dancing" \
        \U1F57A "man dancing" \
        \U1F574 "person in suit levitating" \
        \U1F46F "people with bunny ears" \
        \U1F9D6 "person in steamy room" \
        \U1F9D7 "person climbing" \
        \U1F93A "person fencing" \
        \U1F3C7 "horse racing" \
        \U1F9D8 "person in lotus position" \
        \U1F46D "women holding hands" \
        \U1F46B "woman and man holding hands" \
        \U1F46C "men holding hands" \
        \U1F48F "kiss" \
        \U1F491 "couple with heart" \
        \U1F46A "family" \
        \U1F435 "monkey face" \
        \U1F412 "monkey" \
        \U1F98D "gorilla" \
        \U1F9A7 "orangutan" \
        \U1F436 "dog face" \
        \U1F415 "dog" \
        \U1F9AE "guide dog" \
        \U1F429 "poodle" \
        \U1F9A9 "service dog" \
        \U1F43A "wolf" \
        \U1F98A "fox" \
        \U1F99D "raccoon" \
        \U1F431 "cat face" \
        \U1F408 "cat" \
        \U1F981 "lion" \
        \U1F42F "tiger face" \
        \U1F405 "tiger" \
        \U1F406 "leopard" \
        \U1F434 "horse face" \
        \U1F40E "horse" \
        \U1F984 "unicorn" \
        \U1F993 "zebra" \
        \U1F98C "deer" \
        \U1F42E "cow face" \
        \U1F402 "ox" \
        \U1F403 "water buffalo" \
        \U1F404 "cow" \
        \U1F437 "pig face" \
        \U1F416 "pig" \
        \U1F417 "boar" \
        \U1F43D "pig nose" \
        \U1F40F "ram" \
        \U1F411 "ewe" \
        \U1F410 "goat" \
        \U1F42A "camel" \
        \U1F42B "two hump camel" \
        \U1F999 "llama" \
        \U1F992 "giraffe" \
        \U1F418 "elephant" \
        \U1F98F "rhinoceros" \
        \U1F99B "hippopotamus" \
        \U1F42D "mouse face" \
        \U1F439 "hamster" \
        \U1F430 "rabbit face" \
        \U1F407 "rabbit" \
        \U1F99A "chipmunk" \
        \U1F99C "hedgehog" \
        \U1F438 "frog" \
        \U1F40A "crocodile" \
        \U1F422 "turtle" \
        \U1F98E "lizard" \
        \U1F40D "snake" \
        \U1F432 "dragon face" \
        \U1F409 "dragon" \
        \U1F995 "sauropod" \
        \U1F996 "t rex" \
        \U1F433 "spouting whale" \
        \U1F40B "whale" \
        \U1F42C "dolphin" \
        \U1F41F "fish" \
        \U1F420 "tropical fish" \
        \U1F421 "blowfish" \
        \U1F988 "shark" \
        \U1F419 "octopus" \
        \U1F41A "spiral shell" \
        \U1F980 "crab" \
        \U1F99E "lobster" \
        \U1F990 "shrimp" \
        \U1F991 "squid" \
        \U1F9AA "oyster" \
        \U1F40C "snail" \
        \U1F98B "butterfly" \
        \U1F41B "bug" \
        \U1F41C "ant" \
        \U1F41D "honeybee" \
        \U1F41E "lady beetle" \
        \U1F997 "cricket" \
        \U1F577 "spider" \
        \U1F578 "spider web" \
        \U1F982 "scorpion" \
        \U1F99F "mosquito" \
        \U1F9A0 "microbe" \
        \U1F9A1 "badger" \
        \U1F9A2 "swan" \
        \U1F9A3 "mammoth" \
        \U1F9A4 "dodo" \
        \U1F9A5 "sloth" \
        \U1F9A6 "otter" \
        \U1F9A8 "skunk" \
        \U1F9AB "beaver" \
        \U1F9AC "bison" \
        \U1F9AD "seal" \
        \U1F43E "paw prints" \
        \U1F347 "grapes" \
        \U1F348 "melon" \
        \U1F349 "watermelon" \
        \U1F34A "tangerine" \
        \U1F34B "lemon" \
        \U1F34C "banana" \
        \U1F34D "pineapple" \
        \U1F96D "mango" \
        \U1F34E "red apple" \
        \U1F34F "green apple" \
        \U1F350 "pear" \
        \U1F351 "peach" \
        \U1F352 "cherries" \
        \U1F353 "strawberry" \
        \U1F95D "kiwi fruit" \
        \U1F345 "tomato" \
        \U1F965 "coconut" \
        \U1F951 "avocado" \
        \U1F346 "eggplant" \
        \U1F954 "potato" \
        \U1F955 "carrot" \
        \U1F33D "ear of corn" \
        \U1F336 "hot pepper" \
        \U1F33C "leafy green" \
        \U1F96C "leafy green" \
        \U1F966 "broccoli" \
        \U1F9C4 "garlic" \
        \U1F9C5 "onion" \
        \U1F344 "mushroom" \
        \U1F95C "peanuts" \
        \U1F330 "chestnut" \
        \U1F35E "bread" \
        \U1F950 "croissant" \
        \U1F956 "baguette bread" \
        \U1F968 "pretzel" \
        \U1F9C0 "cheese wedge" \
        \U1F95A "egg" \
        \U1F373 "cooking" \
        \U1F9C8 "butter" \
        \U1F95E "pancakes" \
        \U1F9C7 "waffle" \
        \U1F953 "bacon" \
        \U1F969 "cut of meat" \
        \U1F357 "poultry leg" \
        \U1F356 "meat on bone" \
        \U1F32D "hot dog" \
        \U1F354 "hamburger" \
        \U1F35F "french fries" \
        \U1F355 "pizza" \
        \U1F32E "taco" \
        \U1F32F "burrito" \
        \U1F959 "stuffed flatbread" \
        \U1F9C6 "falafel" \
        \U1F96A "sandwich" \
        \U1F9C3 "beverage box" \
        \U1F958 "shallow pan of food" \
        \U1F35D "spaghetti" \
        \U1F96B "canned food" \
        \U1F35C "steaming bowl" \
        \U1F372 "pot of food" \
        \U1F963 "bowl with spoon" \
        \U1F957 "green salad" \
        \U1F37F "popcorn" \
        \U1F9C2 "salt" \
        \U1F96F "bagel" \
        \U1F95F "dumpling" \
        \U1F960 "fortune cookie" \
        \U1F961 "takeout box" \
        \U1F980 "crab" \
        \U1F365 "fish cake with swirl" \
        \U1F363 "sushi" \
        \U1F364 "fried shrimp" \
        \U1F359 "rice ball" \
        \U1F35A "cooked rice" \
        \U1F358 "rice cracker" \
        \U1F362 "oden" \
        \U1F361 "dango" \
        \U1F367 "shaved ice" \
        \U1F368 "ice cream" \
        \U1F366 "soft ice cream" \
        \U1F967 "pie" \
        \U1F370 "shortcake" \
        \U1F382 "birthday cake" \
        \U1F9C1 "cupcake" \
        \U1F36E "custard" \
        \U1F36D "lollipop" \
        \U1F36C "candy" \
        \U1F36B "chocolate bar" \
        \U1F37F "popcorn" \
        \U1F369 "doughnut" \
        \U1F36A "cookie" \
        \U1F36F "honey pot" \
        \U1F37C "baby bottle" \
        \U1F375 "teacup without handle" \
        \U1F376 "sake" \
        \U1F37E "bottle with popping cork" \
        \U1F377 "wine glass" \
        \U1F378 "cocktail glass" \
        \U1F379 "tropical drink" \
        \U1F37A "beer mug" \
        \U1F37B "clinking beer mugs" \
        \U1F942 "clinking glasses" \
        \U1F943 "tumbler glass" \
        \U1F964 "cup with straw" \
        \U1F9CB "bubble tea" \
        \U1F9C3 "beverage box" \
        \U1F697 "automobile" \
        \U1F695 "taxi" \
        \U1F699 "sport utility vehicle" \
        \U1F68C "bus" \
        \U1F68E "trolleybus" \
        \U1F3CE "racing car" \
        \U1F693 "police car" \
        \U1F691 "ambulance" \
        \U1F692 "fire engine" \
        \U1F690 "minibus" \
        \U1F69A "delivery truck" \
        \U1F69B "articulated lorry" \
        \U1F69C "tractor" \
        \U1F6B2 "bicycle" \
        \U1F6F4 "kick scooter" \
        \U1F6F5 "motor scooter" \
        \U1F6F9 "skateboard" \
        \U1F68F "bus stop" \
        \U1F6A8 "police car light" \
        \U1F694 "oncoming police car" \
        \U1F68D "oncoming bus" \
        \U1F698 "oncoming automobile" \
        \U1F68B "tram car" \
        \U1F682 "locomotive" \
        \U1F683 "railway car" \
        \U1F684 "high speed train" \
        \U1F685 "bullet train" \
        \U1F686 "train" \
        \U1F687 "metro" \
        \U1F688 "light rail" \
        \U1F689 "station" \
        \U1F68A "tram" \
        \U1F69D "monorail" \
        \U1F69E "mountain railway" \
        \U1F6EB "airplane departure" \
        \U1F6EC "airplane arrival" \
        \U1F6E9 "small airplane" \
        \U1F4BA "seat" \
        \U1F680 "rocket" \
        \U1F6F8 "flying saucer" \
        \U1F6F6 "canoe" \
        \U1F6A4 "speedboat" \
        \U1F6F3 "passenger ship" \
        \u26F4 "ferry" \
        \U1F6E5 "motor boat" \
        \U1F6A2 "ship" \
        \U1F6A7 "construction" \
        \U1F6A6 "vertical traffic light" \
        \U1F6A5 "horizontal traffic light" \
        \U1F5FA "world map" \
        \U1F5FF "moai" \
        \U1F3D4 "snow capped mountain" \
        \U1F30B "volcano" \
        \U1F5FB "mount fuji" \
        \U1F3D5 "camping" \
        \U1F3D6 "beach with umbrella" \
        \U1F3DC "desert" \
        \U1F3DD "desert island" \
        \U1F3DE "national park" \
        \U1F3DF "stadium" \
        \U1F3E0 "house" \
        \U1F3E1 "house with garden" \
        \U1F3D8 "houses" \
        \U1F3DA "derelict house" \
        \U1F3D7 "building construction" \
        \U1F3ED "factory" \
        \U1F3E2 "office building" \
        \U1F3EC "department store" \
        \U1F3E3 "japanese post office" \
        \U1F3E4 "european post office" \
        \U1F3E5 "hospital" \
        \U1F3E6 "bank" \
        \U1F3E8 "hotel" \
        \U1F3EA "convenience store" \
        \U1F3EB "school" \
        \U1F3E9 "love hotel" \
        \U1F492 "wedding" \
        \U1F3EF "japanese castle" \
        \U1F3F0 "castle" \
        \U1F3A1 "ferris wheel" \
        \U1F3A2 "roller coaster" \
        \U1F3A0 "carousel horse" \
        \u26F2 "fountain" \
        \u26F0 "mountain" \
        \U1F3C0 "basketball" \
        \U1F3C8 "american football" \
        \U1F3BE "tennis" \
        \U1F94E "softball" \
        \U1F3D0 "volleyball" \
        \U1F3C9 "rugby football" \
        \U1F94F "flying disc" \
        \U1F3B1 "pool 8 ball" \
        \U1F3D3 "ping pong" \
        \U1F3F8 "badminton" \
        \U1F3D2 "ice hockey" \
        \U1F3D1 "field hockey" \
        \U1F94D "lacrosse" \
        \U1F3CF "cricket game" \
        \U1F94A "boxing glove" \
        \U1F94B "martial arts uniform" \
        \U1F945 "goal net" \
        \U1F3A3 "fishing pole" \
        \U1F93F "diving mask" \
        \U1F3BD "running shirt" \
        \U1F3BF "skis" \
        \U1F6F7 "sled" \
        \U1F94C "curling stone" \
        \U1F3C2 "snowboarder" \
        \U1F3CB "person lifting weights" \
        \U1F938 "person cartwheeling" \
        \U1F93C "people wrestling" \
        \U1F93D "person playing water polo" \
        \U1F93E "person playing handball" \
        \U1F939 "person juggling" \
        \U1F3CC "person golfing" \
        \U1F3C4 "person surfing" \
        \U1F6A3 "person rowing boat" \
        \U1F3CA "person swimming" \
        \U1F3C6 "trophy" \
        \U1F947 "first place medal" \
        \U1F948 "second place medal" \
        \U1F949 "third place medal" \
        \U1F3C5 "sports medal" \
        \U1F396 "military medal" \
        \U1F3F5 "rosette" \
        \U1F397 "reminder ribbon" \
        \U1F39F "admission tickets" \
        \U1F3AB "ticket" \
        \U1F3AA "circus tent" \
        \U1F3AD "performing arts" \
        \U1F3A8 "artist palette" \
        \U1F3AC "clapper board" \
        \U1F3A4 "microphone" \
        \U1F3A7 "headphone" \
        \U1F3BC "musical score" \
        \U1F3B9 "musical keyboard" \
        \U1F941 "drum" \
        \U1F3B7 "saxophone" \
        \U1F3BA "trumpet" \
        \U1F3B8 "guitar" \
        \U1F3BB "violin" \
        \U1F3B2 "game die" \
        \U1F3AF "direct hit" \
        \U1F3B3 "bowling" \
        \U1F3AE "video game" \
        \U1F3B0 "slot machine" \
        \U1F9E9 "puzzle piece" \
        \U1F4F1 "mobile phone" \
        \U1F4F2 "mobile phone with arrow" \
        \U1F4BB "laptop" \
        \U1F5A5 "desktop computer" \
        \U1F5A8 "printer" \
        \U1F5B1 "computer mouse" \
        \U1F5B2 "trackball" \
        \U1F579 "joystick" \
        \U1F5DC "clamp" \
        \U1F4BD "computer disk" \
        \U1F4BE "floppy disk" \
        \U1F4BF "optical disk" \
        \U1F4C0 "dvd" \
        \U1F4FC "videocassette" \
        \U1F4F7 "camera" \
        \U1F4F8 "camera with flash" \
        \U1F4F9 "video camera" \
        \U1F3A5 "movie camera" \
        \U1F4FD "film projector" \
        \U1F39E "film frames" \
        \U1F4DE "telephone receiver" \
        \U1F4DF "pager" \
        \U1F4E0 "fax machine" \
        \U1F4FA "television" \
        \U1F4FB "radio" \
        \U1F399 "studio microphone" \
        \U1F39A "level slider" \
        \U1F39B "control knobs" \
        \U1F570 "mantelpiece clock" \
        \U1F4E1 "satellite antenna" \
        \U1F50B "battery" \
        \U1F50C "electric plug" \
        \U1F4A1 "light bulb" \
        \U1F526 "flashlight" \
        \U1F56F "candle" \
        \U1F6E2 "oil drum" \
        \U1F4B8 "money with wings" \
        \U1F4B5 "dollar banknote" \
        \U1F4B4 "yen banknote" \
        \U1F4B6 "euro banknote" \
        \U1F4B7 "pound banknote" \
        \U1F4B0 "money bag" \
        \U1F4B3 "credit card" \
        \U1F48E "gem stone" \
        \U1F9F0 "toolbox" \
        \U1F527 "wrench" \
        \U1F528 "hammer" \
        \U1F6E0 "hammer and wrench" \
        \U1F529 "nut and bolt" \
        \U1F9F1 "brick" \
        \U1F4A3 "bomb" \
        \U1F52A "kitchen knife" \
        \U1F5E1 "dagger" \
        \U1F6E1 "shield" \
        \U1F6AC "cigarette" \
        \U1F52E "crystal ball" \
        \U1F52D "telescope" \
        \U1F52C "microscope" \
        \U1F573 "hole" \
        \U1F9F2 "magnet" \
        \U1F9F3 "lotion bottle" \
        \U1F6BD "toilet" \
        \U1F9F4 "lotion bottle" \
        \U1F9F5 "thread" \
        \U1F9F6 "yarn" \
        \U1F9F7 "safety pin" \
        \U1F9F8 "teddy bear" \
        \U1F9F9 "broom" \
        \U1F9FA "basket" \
        \U1F9FB "roll of paper" \
        \U1F9FC "soap" \
        \U1F9FD "sponge" \
        \U1F9FE "receipt" \
        \U1F9FF "nazar amulet" \
        \U1F9E1 "orange heart" \
        \U1F5A4 "black heart" \
        \U1F90D "white heart" \
        \U1F90E "brown heart" \
        \U1F495 "two hearts" \
        \U1F49E "revolving hearts" \
        \U1F493 "beating heart" \
        \U1F497 "growing heart" \
        \U1F496 "sparkling heart" \
        \U1F498 "heart with arrow" \
        \U1F49D "heart with ribbon" \
        \U1F49F "heart decoration" \
        \U1F6D0 "place of worship" \
        \U1F194 "id button" \
        \U1F251 "japanese acceptable button" \
        \U1F4F4 "mobile phone off" \
        \U1F4F3 "vibration mode" \
        \U1F236 "japanese not free of charge button" \
        \U1F21A "japanese free of charge button" \
        \U1F238 "japanese application button" \
        \U1F23A "japanese open for business button" \
        \U1F237 "japanese monthly amount button" \
        \U1F19A "japanese reserved button" \
        \U1F4AE "white flower" \
        \U1F250 "japanese bargain button" \
        \U1F234 "japanese passing grade button" \
        \U1F235 "japanese no vacancy button" \
        \U1F239 "japanese discount button" \
        \U1F232 "japanese prohibited button" \
        \U1F170 "a button" \
        \U1F171 "b button" \
        \U1F18E "ab button" \
        \U1F191 "cl button" \
        \U1F17E "o button" \
        \U1F198 "sos button" \
        \U1F6D1 "stop sign" \
        \U1F4DB "name badge" \
        \U1F6AB "prohibited" \
        \U1F4AF "hundred points" \
        \U1F4A2 "anger symbol" \
        \U1F6B7 "no pedestrians" \
        \U1F6AF "no littering" \
        \U1F6B3 "no bicycles" \
        \U1F6B1 "non potable water" \
        \U1F51E "no one under eighteen" \
        \U1F4F5 "no mobile phones" \
        \U1F6AD "no smoking" \
        \U1F6B8 "children crossing" \
        \U1F531 "trident emblem" \
    ]
}

# -- helpers

# Return 1 if the emoji is a single code point, or a single code point
# followed by VS16; return 0 for anything longer (ZWJ sequences, etc.).
proc ::tk::emoji::IsSimple {e} {
    set n [string length $e]
    return [expr {$n == 1 || ($n == 2 && [string index $e 1] eq "\uFE0F")}]
}

# Return the ordered, de-duplicated list of simple emoji for a category,
# or the Recent list when the category is "Recent".
proc ::tk::emoji::Items {cat} {
    variable data
    variable recent
    if {$cat eq "Recent"} {return $recent}
    set list [dict get $data $cat]
    set out {}
    set seen {}
    foreach e $list {
        if {![IsSimple $e]} continue
        if {[dict exists $seen $e]} continue
        dict set seen $e 1
        lappend out $e
    }
    return $out
}

# Return 1 when the picker should operate (X11 or Wayland), 0 on the
# platforms that ship their own system emoji picker.
proc ::tk::emoji::Active {} {
    return [expr {[tk windowingsystem] ni {win32 aqua}}]
}

# Return 1 if the widget exists and is a text-entry widget the picker
# knows how to insert into (Entry, TEntry, Text).
proc ::tk::emoji::Supported {w} {
    if {![winfo exists $w]} {return 0}
    set c [winfo class $w]
    return [expr {$c in {Entry TEntry Text}}]
}

# Insert an emoji at the insertion cursor of the target widget. VS16 is
# preserved so the font subsystem picks color presentation.
proc ::tk::emoji::Insert {w s} {
    if {![winfo exists $w]} return
    switch -- [winfo class $w] {
        Entry  { ::tk::EntryInsert $w $s }
        TEntry { ttk::entry::Insert $w $s }
        Text   { ::tk::TextInsert $w $s }
    }
}

# Pick the best available emoji font, preferring color fonts, and cache
# the derived font specs in the namespace state; runs only once.
proc ::tk::emoji::EnsureFonts {} {
    variable S
    if {[info exists S(font)]} return
    # Prefer color emoji fonts; fall back to monochrome ones.
    set candidates {
        {Noto Color Emoji}
        {Apple Color Emoji}
        {Segoe UI Emoji}
        {Twemoji}
        {JoyPixels}
        {EmojiOne Color}
        {Noto Emoji}
        {Noto Sans Symbols2}
        {Noto Sans Symbols}
        {Symbola}
        {DejaVu Sans}
    }
    set family [font actual TkDefaultFont -family]
    set available [font families]
    foreach fam $candidates {
        if {$fam in $available} {
            set family $fam
            break
        }
    }
    set S(font)    [list $family 16]
    set S(catfont) [list $family 9]
    set S(bigfont) [list $family 16]
}

# Look up the hardcoded CLDR TTS name for an emoji (VS16-insensitive);
# fall back to a "U+XXXX" code-point spelling for anything not in the dict.
proc ::tk::emoji::ShortName {e} {
    variable names
    set key [string map {"\uFE0F" ""} $e]
    if {[dict exists $names $key]} {
        return [dict get $names $key]
    }
    set out {}
    foreach cp [split $key ""] {
        scan $cp %c n
        lappend out [format "U+%04X" $n]
    }
    return [join $out " "]
}

# Speak the given text through the accessibility layer if a screen reader
# is running and the speak command is available; otherwise do nothing.
proc ::tk::emoji::Announce {text} {
    if {$text eq ""} return
    if {![llength [info commands ::tk::accessible::speak]]} return
    if {[catch {::tk::accessible::check_screenreader} r]} return
    if {$r ne "1"} return
    catch {::tk::accessible::speak $text}
}

# -- public entry

# Public entry point: show the emoji picker for the given target widget
# (defaulting to the focused widget), building the dialog on first use.
proc ::tk::emoji::choose {{target ""}} {
    variable S
    if {![Active]} {return}
    if {$target eq ""} {set target [focus]}
    if {$target eq "" || ![winfo exists $target]} {bell; return}
    if {[winfo exists $S(top)] && [string match $S(top)* $target]} {return}
    if {![Supported $target]} {bell; return}
    set S(target) $target
    if {![winfo exists $S(top)]} {Build}
    Place
}

# -- building UI

# Create the picker toplevel and all of its child widgets, bindings, and
# category buttons; called once per session on the first choose.
proc ::tk::emoji::Build {} {
    variable S
    variable categories
    variable recent

    EnsureFonts
    set top $S(top)
    set S(drawnW) -1
    set S(cur) -1
    set S(items) {}
    set S(cat) [expr {[llength $recent] ? "Recent" : "Smileys"}]

    toplevel $top -class TkEmojiPicker
    wm withdraw $top
    wm title $top "Emoji"
    wm transient $top [winfo toplevel $S(target)]
    wm overrideredirect $top 0
    wm protocol $top WM_DELETE_WINDOW [namespace code Close]
    bind $top <Escape> [namespace code Close]

    # Minimal colors
    set bg white
    set fg black
    set selbg "#4169E1"
    set selfg white
    set S(bg) $bg; set S(fg) $fg; set S(selbg) $selbg; set S(selfg) $selfg

    ttk::style configure Emoji.Toolbutton -font {"sans-serif" 10} -padding 2 ;#$S(catfont) 

    set f [ttk::frame $top.f -padding 2]
    pack $f -fill both -expand 1

    variable icons
    ttk::frame $f.bar
    foreach cat $categories {
        set key [string tolower $cat]
        set ico [expr {[dict exists $icons $cat] ? [dict get $icons $cat] : ""}]
        ttk::radiobutton $f.bar.$key -style Emoji.Toolbutton \
            -text "$ico $cat" \
            -variable ::tk::emoji::S(cat) -value $cat \
            -command [namespace code SetCategory]
        pack $f.bar.$key -side left -padx 0
    }

    ttk::frame $f.f
    canvas $f.f.c -width [expr {12*$S(cell)}] -height [expr {6*$S(cell)}] \
        -highlightthickness 0 -borderwidth 0 -background $bg -takefocus 1 \
        -yscrollincrement $S(cell)
    ttk::scrollbar $f.f.sb -command [list $f.f.c yview]
    $f.f.c configure -yscrollcommand [list $f.f.sb set]
    pack $f.f.sb -side right -fill y
    pack $f.f.c -side left -fill both -expand 1
    set S(canvas) $f.f.c

    # Override the generic "canvas is not accessible" help text so screen
    # readers describe the picker and its navigation keys.
    if {[llength [info commands ::tk::accessible::set_acc_help]]} {
        catch {
            ::tk::accessible::set_acc_name $S(canvas) "Emoji picker"
            ::tk::accessible::set_acc_role $S(canvas) "List"
            ::tk::accessible::set_acc_help $S(canvas) \
                "Emoji picker. Use arrow keys to move, Return to insert, Escape to close."
        }
    }

    ttk::frame $f.pv
    ttk::label $f.pv.big -font $S(font) -width 3 -anchor center
    ttk::label $f.pv.name -textvariable ::tk::emoji::S(cat) -anchor w -font {"sans-serif" 10} ;#-font $S(catfont)
    pack $f.pv.big -side left
    pack $f.pv.name -side left -padx 4
    set S(prevBig) $f.pv.big

    grid $f.bar -sticky w -pady 1
    grid $f.f   -sticky nsew -pady 1
    grid $f.pv  -sticky ew
    grid columnconfigure $f 0 -weight 1
    grid rowconfigure $f 1 -weight 1

    set c $S(canvas)
    bind $c <Configure>           [namespace code {Reflow %w}]
    bind $c <Motion>              [namespace code {Hover %x %y}]
    bind $c <ButtonPress-1>       [namespace code {Click %x %y 0}]
    bind $c <Shift-ButtonPress-1> [namespace code {Click %x %y 1}]
    bind $c <MouseWheel>          [namespace code {Wheel %D}]
    bind $c <Button-4>            [namespace code {Wheel 1}]
    bind $c <Button-5>            [namespace code {Wheel -1}]
    bind $c <Key-Left>            [namespace code {Move left}]
    bind $c <Key-Right>           [namespace code {Move right}]
    bind $c <Key-Up>              [namespace code {Move up}]
    bind $c <Key-Down>            [namespace code {Move down}]
    bind $c <Key-Home>            [namespace code {Move home}]
    bind $c <Key-End>             [namespace code {Move end}]
    bind $c <Return>              [namespace code {PickCurrent 0}]
    bind $c <space>               [namespace code {PickCurrent 0}]
    bind $c <Shift-Return>        [namespace code {PickCurrent 1}]
}

# Compute and apply a screen position for the picker just below the
# insertion cursor of the target, clamped to stay on screen.
proc ::tk::emoji::Place {} {
    variable S
    set top $S(top)
    set t $S(target)
    update idletasks
    set w [winfo reqwidth $top]
    set h [winfo reqheight $top]
    set x [winfo rootx $t]
    set y [expr {[winfo rooty $t] + [winfo height $t]}]
    catch {
        lassign [$t bbox insert] bx by bw bh
        set x [expr {[winfo rootx $t] + $bx}]
        set y [expr {[winfo rooty $t] + $by + $bh + 2}]
    }
    set sw [winfo screenwidth $t]
    set sh [winfo screenheight $t]
    if {$x+$w > $sw} {set x [expr {$sw-$w}]}
    if {$y+$h > $sh} {set y [expr {[winfo rooty $t]-$h-2}]}
    wm geometry $top +[expr {max(0,$x)}]+[expr {max(0,$y)}]
    wm deiconify $top
    raise $top
    focus -force $S(canvas)
}

# Destroy the picker window and restore focus to the target widget if it
# still exists.
proc ::tk::emoji::Close {} {
    variable S
    set t $S(target)
    destroy $S(top)
    if {[winfo exists $t]} {catch {focus -force $t}}
}

# -- draw / nav

# Command invoked by the category radio buttons; redraws from the top of
# the new category's list.
proc ::tk::emoji::SetCategory {} { Draw 0 }

# Handle a canvas width change by redrawing when the column count would
# actually change.
proc ::tk::emoji::Reflow {width} {
    variable S
    if {$width != $S(drawnW)} {Draw 1}
}

# Translate an item index into canvas x/y coordinates for the top-left
# corner of that item's cell.
proc ::tk::emoji::Origin {idx} {
    variable S
    set col [expr {$idx % $S(cols)}]
    set row [expr {$idx / $S(cols)}]
    return [list [expr {$S(x0)+$col*$S(cell)}] [expr {$row*$S(cell)}]]
}

# Rebuild the canvas contents for the current category, recomputing the
# column layout and optionally preserving scroll and cursor position.
proc ::tk::emoji::Draw {keep} {
    variable S
    set c $S(canvas)
    set cell $S(cell)
    set oldcur $S(cur)
    set frac [lindex [$c yview] 0]
    $c delete all
    set S(cur) -1
    set S(items) [Items $S(cat)]
    set S(drawnW) [winfo width $c]
    set width $S(drawnW)
    if {$width < $cell} {set width [$c cget -width]}
    set S(cols) [expr {max(1,$width/$cell)}]
    set S(x0) [expr {($width - $S(cols)*$cell)/2}]
    $c create rectangle 0 0 0 0 -fill $S(selbg) -outline {} -state hidden -tags cursor
    set i 0
    foreach e $S(items) {
        lassign [Origin $i] x y
        $c create text [expr {$x+$cell/2}] [expr {$y+$cell/2}] \
            -text $e -font $S(font) -fill $S(fg) -tags [list emoji e$i]
        incr i
    }
    if {$i==0} {
        $c create text [expr {$width/2}] [expr {$cell}] -fill gray50 \
            -text "Nothing yet" -font TkDefaultFont
    }
    $c lower cursor
    set rows [expr {($i+$S(cols)-1)/$S(cols)}]
    set S(total) [expr {max(1,$rows)*$cell}]
    $c configure -scrollregion [list 0 0 $width $S(total)]
    if {$keep} {
        $c yview moveto $frac
        if {$oldcur>=0 && $oldcur<$i} {SetCur $oldcur}
    } else {
        $c yview moveto 0
        $S(prevBig) configure -text ""
    }
}

# Move the selection highlight to the given item index, update the preview
# label, and announce the emoji to a running screen reader.
proc ::tk::emoji::SetCur {idx} {
    variable S
    set c $S(canvas)
    if {$idx==$S(cur)} return
    if {$S(cur)>=0} {$c itemconfigure e$S(cur) -fill $S(fg)}
    set S(cur) $idx
    if {$idx<0} {
        $c itemconfigure cursor -state hidden
        $S(prevBig) configure -text ""
        return
    }
    set cell $S(cell)
    lassign [Origin $idx] x y
    $c coords cursor [expr {$x+1}] [expr {$y+1}] [expr {$x+$cell-1}] [expr {$y+$cell-1}]
    $c itemconfigure cursor -state normal
    $c itemconfigure e$idx -fill $S(selfg)
    set glyph [lindex $S(items) $idx]
    $S(prevBig) configure -text $glyph
    # Speak the CLDR TTS name so the user hears what's under the cursor.
    Announce [ShortName $glyph]
}

# Scroll the canvas just enough so the given item index is visible within
# the current viewport.
proc ::tk::emoji::EnsureVisible {idx} {
    variable S
    set c $S(canvas); set h [winfo height $c]
    if {$S(total)<=$h} return
    set y [lindex [Origin $idx] 1]
    set top [$c canvasy 0]
    if {$y<$top} {
        $c yview moveto [expr {double($y)/$S(total)}]
    } elseif {$y+$S(cell) > $top+$h} {
        $c yview moveto [expr {double($y+$S(cell)-$h)/$S(total)}]
    }
}

# Convert canvas x/y pixel coordinates to an item index, returning -1 if
# the point is outside the grid or past the last emoji.
proc ::tk::emoji::IndexAt {x y} {
    variable S
    set c $S(canvas)
    set cx [expr {[$c canvasx $x]-$S(x0)}]
    set cy [$c canvasy $y]
    if {$cx<0 || $cy<0} {return -1}
    set col [expr {int($cx)/$S(cell)}]
    set row [expr {int($cy)/$S(cell)}]
    if {$col>=$S(cols)} {return -1}
    set idx [expr {$row*$S(cols)+$col}]
    if {$idx >= [llength $S(items)]} {return -1}
    return $idx
}

# Mouse-motion handler: highlight the emoji under the pointer.
proc ::tk::emoji::Hover {x y} {SetCur [IndexAt $x $y]}

# Mouse-wheel handler: scroll the canvas one item up or down.
proc ::tk::emoji::Wheel {d} {variable S; $S(canvas) yview scroll [expr {$d>0 ? -1 : 1}] units}

# Keyboard navigation handler for left/right/up/down/home/end; clamps the
# resulting index to the current list and scrolls it into view.
proc ::tk::emoji::Move {how} {
    variable S; set n [llength $S(items)]; if {$n==0} return
    set i $S(cur)
    switch -- $how {
        home {set i 0}
        end  {set i [expr {$n-1}]}
        default {
            if {$i<0} {set i 0} else {
                switch -- $how {
                    left  {incr i -1}
                    right {incr i}
                    up    {incr i [expr {-$S(cols)}]}
                    down  {incr i $S(cols)}
                }
            }
        }
    }
    set i [expr {max(0,min($n-1,$i))}]
    SetCur $i; EnsureVisible $i
}

# Mouse click handler: pick the emoji under the pointer, or do nothing if
# the click landed outside the grid.
proc ::tk::emoji::Click {x y keep} {
    set i [IndexAt $x $y]; if {$i>=0} {Pick $i $keep}
}

# Return/Space handler: pick the currently highlighted emoji if any.
proc ::tk::emoji::PickCurrent {keep} {
    variable S; if {$S(cur)>=0} {Pick $S(cur) $keep}
}

# Insert the emoji at the given index into the target, push it onto the
# recent list, announce it, and close the picker unless keep is set.
proc ::tk::emoji::Pick {idx keep} {
    variable S; variable recent; variable maxRecent
    set e [lindex $S(items) $idx]
    if {![winfo exists $S(target)]} {Close; return}
    Insert $S(target) $e
    set recent [linsert [lsearch -all -inline -not -exact $recent $e] 0 $e]
    set recent [lrange $recent 0 [expr {$maxRecent-1}]]
    # Confirm insertion to the screen reader. Do this before Close so the
    # announcement isn't lost if closing tears down state we depend on.
    Announce "inserted [ShortName $e]"
    if {!$keep} {Close}
}

# -- bindings

if {[::tk::emoji::Active]} {
    event add <<EmojiPicker>> <Control-period>
    event add <<EmojiPicker>> <Control-KP_Decimal>
    event add <<EmojiPicker>> <Control-semicolon>

    # Super (Mod4) variants. A compositor may reserve these for itself, in
    # which case Tk never sees them.
    event add <<EmojiPicker>> <Mod4-period>
    event add <<EmojiPicker>> <Mod4-semicolon>

    # Class bindings with break, so the key is handled here and only once.
    foreach cls {Entry TEntry Text} {
        bind $cls <<EmojiPicker>> {::tk::emoji::choose %W; break}
    }
}

# demo when run directly
if {[info exists ::argv0] && [info script] ne "" \
        && [file normalize $::argv0] eq [file normalize [info script]]} {
    wm title . "Emoji picker demo"
    ttk::frame .f -padding 10; pack .f -fill both -expand 1
    ttk::label .f.l1 -text "Entry (Ctrl-. or Super-.) :"
    ttk::entry .f.e -width 40
    ttk::label .f.l2 -text "Text (Ctrl-. or Super-.) :"
    text .f.t -width 40 -height 6
    pack .f.l1 -anchor w; pack .f.e -fill x -pady {2 8}
    pack .f.l2 -anchor w; pack .f.t -fill both -expand 1
    focus .f.e
}
