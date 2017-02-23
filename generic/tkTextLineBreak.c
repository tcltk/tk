/*
 * tkTextLineBreak.c --
 *
 *	This module provides line break computation for line wrapping.
 *	It uses the library "libunibreak" (from Wu Yongwei) for the
 *	computation, but only if available (currently only UNIX), and if
 *	the language support is enabled, otherwise our own line break
 *	algorithm is used (it's a simplified version of the recommendation
 *	at http://www.unicode.org/reports/tr14/tr14-26.html).
 *
 *	The alternative is the use of ICU library (http://site.icu-project.org/),
 *	instead of libunibreak, but this would require to support a very
 *	complex interface of a dynamic load library, with other words, we
 *	would need dozens of functions pointers. This is not really a drawback,
 *	and probably the ICU library is the better choice, but I think that a
 *	change to the ICU library is reasonable only if the Tcl/Tk developer team
 *	is deciding to use this library also for complete Unicode support (character
 *	conversion, for instance).
 *
 * Copyright (c) 2015-2017 Gregor Cramer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkText.h"

#include <ctype.h>
#include <assert.h>

#ifndef MAX
# define MAX(a,b) (((int) a) < ((int) b) ? b : a)
#endif


typedef void (*ComputeBreakLocationsFunc)(
    const unsigned char *text, size_t len, const char *lang, char *brks);

static void ComputeBreakLocations(
    const unsigned char *text, size_t len, const char *lang, char *brks);

static ComputeBreakLocationsFunc libLinebreakFunc = ComputeBreakLocations;

/*
 *----------------------------------------------------------------------
 *
 * GetLineBreakFunc --
 *
 *	Return the appropriate line break function. If argument 'lang'
 *	is NULL, then our own line break alorithm will be used (fast,
 *	but a bit simple). If 'lang' is not NULL, then this function
 *	tries to load the library "libunibreak" (currently only UNIX).
 *	If the load succeeds, then set_linebreaks_utf8 will be returned,
 *	otherwise ComputeBreakLocations will be returned.
 *
 *	Note that "libunibreak" has language specific support, but
 *	currently only for zh, ja, and ko. Nethertheless any non-NULL
 *	value for 'lang' tries to use this library.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The "libunibreak" library may be loaded, if available.
 *
 *----------------------------------------------------------------------
 */

#ifdef __UNIX__

static int
LoadFile(
    Tcl_Interp *interp,
    Tcl_Obj *pathPtr,
    Tcl_LoadHandle *handle,
    char const **symbols,
    void **funcs)
{
    /* Keep backward compatibility to 8.5 */
# if TCL_MAJOR_VERSION == 8 && TCL_MINOR_VERSION == 5
    return Tcl_FSLoadFile(interp, pathPtr, symbols[0], symbols[1],
	    (void *) &funcs[0], (void *) &funcs[1], handle, NULL);
# else
    return Tcl_LoadFile(interp, pathPtr, symbols, TCL_LOAD_GLOBAL, funcs, handle);
# endif
}

static void
LoadLibUnibreak(
    Tcl_Interp *interp)
{
    typedef void *VoidP;
    typedef void (*InitFunc)();

    static char const *Symbols[3] = {
	"init_linebreak",
	"set_linebreaks_utf8",
	NULL
    };

    VoidP Funcs[sizeof(Symbols)/sizeof(Symbols[0])];
    Tcl_LoadHandle handle;
    Tcl_Obj *pathPtr = Tcl_NewStringObj("libunibreak.so.1", -1);
    bool rc;

    Tcl_IncrRefCount(pathPtr);
    rc = LoadFile(interp, pathPtr, &handle, Symbols, Funcs);
    if (rc != TCL_OK) {
	/*
	 * We couldn't find "libunibreak.so.1", so try the predecessor "liblinebreak.so.2".
	 */

	Tcl_ResetResult(interp);
	Tcl_DecrRefCount(pathPtr);
	pathPtr = Tcl_NewStringObj("liblinebreak.so.2", -1);
	rc = LoadFile(interp, pathPtr, &handle, Symbols, Funcs);
    }
    if (rc == TCL_OK) {
	((InitFunc) Funcs[0])();
	libLinebreakFunc = Funcs[1];
    } else {
	Tcl_ResetResult(interp);
    }
    Tcl_DecrRefCount(pathPtr);
}

#endif /* __UNIX__ */

static ComputeBreakLocationsFunc
GetLineBreakFunc(
    Tcl_Interp *interp,
    char const *lang)
{
#ifdef __UNIX__
    if (lang) {
	static bool loaded = false;

	if (!loaded) {
	    LoadLibUnibreak(interp);
	}
    }
#endif
    return libLinebreakFunc;
}

/*
 *----------------------------------------------------------------------
 *
 * TkTextComputeBreakLocations --
 *
 *	Compute break locations in UTF-8 text. This function expects
 *	a nul-terminated string (this mean that the character at position
 *	'len' must be NUL). Thus it is also required that the break buffer
 *	'brks' has at least size 'len+1'. If 'lang' is not NULL, then the
 *	external library linunibreak will be used for the line break
 *	computation, but only if this library is loadable, otherwise the
 *	internal algorithm will be used.
 *
 * Results:
 *	The computed break locations. This function returns 'true' if
 *	the external linebreak library has been used for the computation,
 *	otherwise 'false' will be returned.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

bool
TkTextComputeBreakLocations(
    Tcl_Interp *interp,
    const char *text,	/* must be nul-terminated */
    unsigned len,	/* without trailing nul byte */
    const char *lang,	/* can be NULL */
    char *brks)
{
    ComputeBreakLocationsFunc func;
    int lastBreakablePos = -1;
    unsigned i;

    assert(text);
    assert(brks);
    assert(text[len] == '\0');
    assert(!lang || (isalpha(lang[0]) && isalpha(lang[1]) && !lang[2]));

    func = GetLineBreakFunc(interp, lang);

    /*
     * The algorithm don't give us a break value for the last character if we do
     * not include the final nul char into the computation.
     */

    len += 1;
    (*func)((const unsigned char *) text, len, lang, brks);
    len -= 1;

    for (i = 0; i < len; ++i) {
	switch (brks[i]) {
	case LINEBREAK_MUSTBREAK:
	    break;
	case LINEBREAK_ALLOWBREAK:
	    if (text[i] == '-') {
		if (brks[i] == LINEBREAK_ALLOWBREAK) {
		    /*
		     * Fix the problem with the contextual hyphen-minus sign, the implementation of
		     * libunibreak has (possibly) forgotten this case.
		     *
		     * The hyphen-minus (U+002D) needs special context treatment. For simplicity we
		     * will only check whether we have two preceding, and two succeeding letters.
		     * TODO: Is there a better method for the decision?
		     */

		    const char *r = text + i;
		    const char *p, *q, *s;
		    Tcl_UniChar uc;
		    bool allow = false;

		    q = Tcl_UtfPrev(r, text);
		    if (q != r) {
			Tcl_UtfToUniChar(q, &uc);
			if (Tcl_UniCharIsAlpha(uc)) {
			    p = Tcl_UtfPrev(q, text);
			    if (p != q) {
				Tcl_UtfToUniChar(p, &uc);
				if (Tcl_UniCharIsAlpha(uc)) {
				    s = r + 1;
				    s += Tcl_UtfToUniChar(s, &uc);
				    if (Tcl_UniCharIsAlpha(uc)) {
					Tcl_UtfToUniChar(s, &uc);
					if (Tcl_UniCharIsAlpha(uc)) {
					    allow = true;
					}
				    }
				}
			    }
			}
		    }

		    if (!allow) {
			brks[i] = LINEBREAK_NOBREAK;
		    }
		}
	    } else if (text[i] == '/' && i > 8) {
		/*
		 * Ignore the breaking chance if there is a chance immediately before:
		 * no break inside "c/o", and no break after "http://" in a long line
		 * (a suggestion from Wu Yongwei).
		 */

		if (lastBreakablePos >= (int) i - 2
			|| (i > 40u && lastBreakablePos >= (int) i - 7 && text[i - 1] == '/')) {
		    continue;
		}

		/*
		 * Special rule to treat Unix paths more nicely (a suggestion from Wu Yongwei).
		 */

		if (i < len - 1 && text[i + 1] != ' ' && text[i - 1] == ' ') {
		    lastBreakablePos = i - 1;
		    continue;
		}
	    }
	    lastBreakablePos = i;
	    break;
        case LINEBREAK_INSIDEACHAR:
	    break;
	}
    }

    return func != ComputeBreakLocations;
}

/*
 * The following is implementing the recommendations at
 * http://www.unicode.org/reports/tr14/tr14-26.html, but simplified -
 * no language specific support, not all the rules (especially no
 * combining marks), and mostly restricted to Latin-1 and relevant
 * letters not belonging to specific languages. For a more sophisticated
 * line break algorithm the library "libunibreak" (from Wu Yongwei)
 * should be used.
 */

typedef enum {
    /* Note that CR, LF, and NL will be interpreted as BK, so only BK is used. */
    AI, AL, B2, BA, BB, BK, CL, CP, EX, GL, HY, IN, IS, NS, NU, OP, PO, PR, QU, SP, SY, WJ, ZW
} LBClass;

#define __ AI

/*
 * Changes in table below (different from Unicode recommendation):
 *
 * 0a: CB -> BK	(LINE FEED)
 * 0d: CR -> BK (CARRIAGE RETURN)
 * 0e: XX -> BK (SHIFT OUT)
 * 23: AL -> IN (NUMBER SIGN)
 * 26: AL -> BB (AMPERSAND)
 * 3d: AL -> GL (EQUALS SIGN)
 * 60: CM -> AL (GRAVE ACCENT)
 */

static const char Table_0000[256] = {
/*      00  01  02  03  04  05  06  07  08  09  0a  0b  0c  0d  0e  0f */
/* 0 */ __, __, __, __, __, __, __, __, __, BA, BK, BK, BK, BK, BK, __, /* 00 - 0f */
/* 1 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 10 - 1f */
/* 2 */ SP, EX, QU, IN, PR, PO, BB, QU, OP, CP, AL, PR, IS, HY, IS, SY, /* 20 - 2f */
/* 3 */ NU, NU, NU, NU, NU, NU, NU, NU, NU, NU, IS, IS, AL, GL, AL, EX, /* 30 - 3f */
/* 4 */ AL, AL, AL, AL, AL, AL, AL, AL, AL, AL, AL, AL, AL, AL, AL, AL, /* 40 - 4f */
/* 5 */ AL, AL, AL, AL, AL, AL, AL, AL, AL, AL, AL, OP, PR, CP, AL, AL, /* 50 - 5f */
/* 6 */ AL, AL, AL, AL, AL, AL, AL, AL, AL, AL, AL, AL, AL, AL, AL, AL, /* 60 - 6f */
/* 7 */ AL, AL, AL, AL, AL, AL, AL, AL, AL, AL, AL, OP, BA, CL, AL, __, /* 70 - 7f */
/* 8 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 80 - 8f */
/* 9 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 90 - 9f */
/* a */ GL, OP, PO, PR, PR, PR, AL, AL, AL, AL, __, QU, __, __, AL, AL, /* a0 - af */
/* b */ PO, PR, AL, AL, BB, __, AL, AL, AL, AL, __, __, AL, AL, AL, OP, /* b0 - bf */
/* c */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* c0 - cf */
/* d */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* d0 - df */
/* e */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* e0 - ef */
/* f */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* f0 - ff */
/*      00  01  02  03  04  05  06  07  08  09  0a  0b  0c  0d  0e  0f */
};

/*
 * Changes in table below (different from Unicode recommendation):
 *
 * e2 80 89: BA -> WJ (THIN SPACE)
 * e2 80 0a: BA -> WJ (HAIR SPACE)
 */

static const char Table_E280[256] = {
/*      00  01  02  03  04  05  06  07  08  09  0a  0b  0c  0d  0e  0f */
/* 0 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 00 - 0f */
/* 1 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 10 - 1f */
/* 2 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 20 - 2f */
/* 3 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 30 - 3f */
/* 4 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 40 - 4f */
/* 5 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 50 - 5f */
/* 6 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 60 - 6f */
/* 7 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 70 - 7f */
/* 8 */ BA, BA, BA, BA, BA, BA, BA, GL, BA, __, __, ZW, __, __, __, __, /* 80 - 8f */
/* 9 */ BA, AL, BA, BA, B2, AL, AL, AL, QU, QU, OP, QU, QU, QU, OP, QU, /* 90 - 9f */
/* a */ AL, AL, AL, AL, IN, IN, IN, BA, BK, BK, __, __, __, __, __, GL, /* a0 - af */
/* b */ PO, PO, PO, PO, PO, PO, PO, PO, AL, QU, QU, AL, NS, NS, AL, AL, /* b0 - bf */
/* c */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* c0 - cf */
/* d */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* d0 - df */
/* e */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* e0 - ef */
/* f */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* f0 - ff */
/*      00  01  02  03  04  05  06  07  08  09  0a  0b  0c  0d  0e  0f */
};

static const char Table_E281[256] = {
/*      00  01  02  03  04  05  06  07  08  09  0a  0b  0c  0d  0e  0f */
/* 0 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 00 - 0f */
/* 1 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 10 - 1f */
/* 2 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 20 - 2f */
/* 3 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 30 - 3f */
/* 4 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 40 - 4f */
/* 5 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 50 - 5f */
/* 6 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 60 - 6f */
/* 7 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 70 - 7f */
/* 8 */ AL, AL, AL, AL, IS, OP, CL, NS, NS, NS, AL, AL, AL, AL, AL, AL, /* 80 - 8f */
/* 9 */ AL, AL, __, AL, AL, AL, AL, AL, AL, AL, AL, AL, AL, AL, AL, __, /* 90 - 9f */
/* a */ WJ, AL, AL, AL, AL, __, __, __, __, __, __, __, __, __, __, __, /* a0 - af */
/* b */ __, __, __, __, __, __, __, __, __, __, __, __, __, OP, CL, __, /* b0 - bf */
/* c */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* c0 - cf */
/* d */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* d0 - df */
/* e */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* e0 - ef */
/* f */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* f0 - ff */
/*      00  01  02  03  04  05  06  07  08  09  0a  0b  0c  0d  0e  0f */
};

static const char Table_E282[256] = {
/*      00  01  02  03  04  05  06  07  08  09  0a  0b  0c  0d  0e  0f */
/* 0 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 00 - 0f */
/* 1 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 10 - 1f */
/* 2 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 20 - 2f */
/* 3 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 30 - 3f */
/* 4 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 40 - 4f */
/* 5 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 50 - 5f */
/* 6 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 60 - 6f */
/* 7 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 70 - 7f */
/* 8 */ __, __, __, __, __, __, __, __, __, __, __, __, __, CL, CL, __, /* 80 - 8f */
/* 9 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 90 - 9f */
/* a */ PR, PR, PR, PR, PR, PR, PR, PO, PR, PR, PR, PR, PR, PR, PR, PR, /* a0 - af */
/* b */ PR, PR, PR, PR, PR, PR, PR, PR, PR, PR, PR, PR, PR, PR, PR, __, /* b0 - bf */
/* c */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* c0 - cf */
/* d */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* d0 - df */
/* e */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* e0 - ef */
/* f */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* f0 - ff */
/*      00  01  02  03  04  05  06  07  08  09  0a  0b  0c  0d  0e  0f */
};

static const char Table_E28C[256] = {
/*      00  01  02  03  04  05  06  07  08  09  0a  0b  0c  0d  0e  0f */
/* 0 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 00 - 0f */
/* 1 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 10 - 1f */
/* 2 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 20 - 2f */
/* 3 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 30 - 3f */
/* 4 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 40 - 4f */
/* 5 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 50 - 5f */
/* 6 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 60 - 6f */
/* 7 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 70 - 7f */
/* 8 */ __, __, __, __, __, __, __, __, OP, CL, OP, CL, __, __, __, __, /* 80 - 8f */
/* 9 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 90 - 9f */
/* a */ __, __, __, __, __, __, __, __, __, OP, CL, __, __, __, __, __, /* a0 - af */
/* b */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* b0 - bf */
/* c */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* c0 - cf */
/* d */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* d0 - df */
/* e */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* e0 - ef */
/* f */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* f0 - ff */
/*      00  01  02  03  04  05  06  07  08  09  0a  0b  0c  0d  0e  0f */
};

static const char Table_E29D[256] = {
/*      00  01  02  03  04  05  06  07  08  09  0a  0b  0c  0d  0e  0f */
/* 0 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 00 - 0f */
/* 1 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 10 - 1f */
/* 2 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 20 - 2f */
/* 3 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 30 - 3f */
/* 4 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 40 - 4f */
/* 5 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 50 - 5f */
/* 6 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 60 - 6f */
/* 7 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 70 - 7f */
/* 8 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 80 - 8f */
/* 9 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 90 - 9f */
/* a */ __, __, __, __, __, __, __, __, OP, CL, OP, CL, OP, CL, OP, CL, /* a0 - af */
/* b */ OP, CL, OP, CL, OP, CL, __, __, __, __, __, __, __, __, __, __, /* b0 - bf */
/* c */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* c0 - cf */
/* d */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* d0 - df */
/* e */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* e0 - ef */
/* f */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* f0 - ff */
/*      00  01  02  03  04  05  06  07  08  09  0a  0b  0c  0d  0e  0f */
};

static const char Table_E29F[256] = {
/*      00  01  02  03  04  05  06  07  08  09  0a  0b  0c  0d  0e  0f */
/* 0 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 00 - 0f */
/* 1 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 10 - 1f */
/* 2 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 20 - 2f */
/* 3 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 30 - 3f */
/* 4 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 40 - 4f */
/* 5 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 50 - 5f */
/* 6 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 60 - 6f */
/* 7 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 70 - 7f */
/* 8 */ __, __, __, __, __, OP, CL, __, __, __, __, __, __, __, __, __, /* 80 - 8f */
/* 9 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 90 - 9f */
/* a */ __, __, __, __, __, __, OP, CL, OP, CL, OP, CL, OP, CL, OP, CL, /* a0 - af */
/* b */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* b0 - bf */
/* c */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* c0 - cf */
/* d */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* d0 - df */
/* e */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* e0 - ef */
/* f */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* f0 - ff */
/*      00  01  02  03  04  05  06  07  08  09  0a  0b  0c  0d  0e  0f */
};

static const char Table_E2A6[256] = {
/*      00  01  02  03  04  05  06  07  08  09  0a  0b  0c  0d  0e  0f */
/* 0 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 00 - 0f */
/* 1 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 10 - 1f */
/* 2 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 20 - 2f */
/* 3 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 30 - 3f */
/* 4 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 40 - 4f */
/* 5 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 50 - 5f */
/* 6 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 60 - 6f */
/* 7 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 70 - 7f */
/* 8 */ __, __, __, OP, CL, OP, CL, OP, CL, OP, CL, OP, CL, OP, CL, OP, /* 80 - 8f */
/* 9 */ CL, OP, CL, OP, CL, OP, CL, OP, CL, __, __, __, __, __, __, __, /* 90 - 9f */
/* a */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* a0 - af */
/* b */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* b0 - bf */
/* c */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* c0 - cf */
/* d */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* d0 - df */
/* e */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* e0 - ef */
/* f */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* f0 - ff */
/*      00  01  02  03  04  05  06  07  08  09  0a  0b  0c  0d  0e  0f */
};

static const char Table_E2A7[256] = {
/*      00  01  02  03  04  05  06  07  08  09  0a  0b  0c  0d  0e  0f */
/* 0 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 00 - 0f */
/* 1 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 10 - 1f */
/* 2 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 20 - 2f */
/* 3 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 30 - 3f */
/* 4 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 40 - 4f */
/* 5 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 50 - 5f */
/* 6 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 60 - 6f */
/* 7 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 70 - 7f */
/* 8 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 80 - 8f */
/* 9 */ __, __, __, __, __, __, __, __, OP, CL, OP, CL, __, __, __, __, /* 90 - 9f */
/* a */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* a0 - af */
/* b */ __, __, __, __, __, __, __, __, __, __, __, __, OP, CL, __, __, /* b0 - bf */
/* c */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* c0 - cf */
/* d */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* d0 - df */
/* e */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* e0 - ef */
/* f */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* f0 - ff */
/*      00  01  02  03  04  05  06  07  08  09  0a  0b  0c  0d  0e  0f */
};

static const char Table_E2B8[256] = {
/*      00  01  02  03  04  05  06  07  08  09  0a  0b  0c  0d  0e  0f */
/* 0 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 00 - 0f */
/* 1 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 10 - 1f */
/* 2 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 20 - 2f */
/* 3 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 30 - 3f */
/* 4 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 40 - 4f */
/* 5 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 50 - 5f */
/* 6 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 60 - 6f */
/* 7 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 70 - 7f */
/* 8 */ AL, AL, QU, QU, QU, QU, AL, AL, AL, QU, QU, AL, QU, QU, AL, AL, /* 80 - 8f */
/* 9 */ AL, AL, AL, AL, AL, AL, AL, AL, OP, AL, AL, AL, QU, QU, AL, AL, /* 90 - 9f */
/* a */ QU, QU, OP, CL, OP, CL, OP, CL, OP, CL, AL, AL, AL, AL, AL, __, /* a0 - af */
/* b */ AL, AL, AL, AL, AL, AL, AL, AL, AL, AL, B2, B2, AL, AL, AL, AL, /* b0 - bf */
/* c */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* c0 - cf */
/* d */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* d0 - df */
/* e */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* e0 - ef */
/* f */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* f0 - ff */
/*      00  01  02  03  04  05  06  07  08  09  0a  0b  0c  0d  0e  0f */
};

static const char Table_E380[256] = {
/*      00  01  02  03  04  05  06  07  08  09  0a  0b  0c  0d  0e  0f */
/* 0 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 00 - 0f */
/* 1 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 10 - 1f */
/* 2 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 20 - 2f */
/* 3 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 30 - 3f */
/* 4 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 40 - 4f */
/* 5 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 50 - 5f */
/* 6 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 60 - 6f */
/* 7 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 70 - 7f */
/* 8 */ __, CL, CL, AL, __, NS, __, __, OP, CL, OP, CL, OP, CL, OP, CL, /* 80 - 8f */
/* 9 */ OP, CL, __, __, OP, CL, OP, CL, OP, CL, OP, CL, NS, OP, CL, CL, /* 90 - 9f */
/* a */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* a0 - af */
/* b */ AL, __, __, __, __, __, __, __, __, __, __, NS, NS, AL, __, __, /* b0 - bf */
/* c */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* c0 - cf */
/* d */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* d0 - df */
/* e */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* e0 - ef */
/* f */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* f0 - ff */
/*      00  01  02  03  04  05  06  07  08  09  0a  0b  0c  0d  0e  0f */
};

static const char Table_EFB8[256] = {
/*      00  01  02  03  04  05  06  07  08  09  0a  0b  0c  0d  0e  0f */
/* 0 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 00 - 0f */
/* 1 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 10 - 1f */
/* 2 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 20 - 2f */
/* 3 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 30 - 3f */
/* 4 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 40 - 4f */
/* 5 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 50 - 5f */
/* 6 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 60 - 6f */
/* 7 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 70 - 7f */
/* 8 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 80 - 8f */
/* 9 */ IS, CL, CL, IS, IS, AL, AL, OP, CL, IN, __, __, __, __, __, __, /* 90 - 9f */
/* a */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* a0 - af */
/* b */ AL, AL, AL, AL, AL, OP, CL, OP, CL, OP, CL, OP, CL, OP, CL, OP, /* b0 - bf */
/* c */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* c0 - cf */
/* d */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* d0 - df */
/* e */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* e0 - ef */
/* f */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* f0 - ff */
/*      00  01  02  03  04  05  06  07  08  09  0a  0b  0c  0d  0e  0f */
};

static const char Table_EFB9[256] = {
/*      00  01  02  03  04  05  06  07  08  09  0a  0b  0c  0d  0e  0f */
/* 0 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 00 - 0f */
/* 1 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 10 - 1f */
/* 2 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 20 - 2f */
/* 3 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 30 - 3f */
/* 4 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 40 - 4f */
/* 5 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 50 - 5f */
/* 6 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 60 - 6f */
/* 7 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 70 - 7f */
/* 8 */ CL, OP, CL, OP, CL, AL, AL, OP, CL, AL, AL, AL, AL, AL, AL, AL, /* 80 - 8f */
/* 9 */ CL, CL, CL, __, NS, NS, AL, AL, B2, OP, CL, OP, CL, OP, CL, AL, /* 90 - 9f */
/* a */ AL, AL, __, B2, __, __, __, __, AL, PR, PO, AL, __, __, __, __, /* a0 - af */
/* b */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* b0 - bf */
/* c */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* c0 - cf */
/* d */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* d0 - df */
/* e */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* e0 - ef */
/* f */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* f0 - ff */
/*      00  01  02  03  04  05  06  07  08  09  0a  0b  0c  0d  0e  0f */
};

static const char Table_EFBC[256] = {
/*      00  01  02  03  04  05  06  07  08  09  0a  0b  0c  0d  0e  0f */
/* 0 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 00 - 0f */
/* 1 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 10 - 1f */
/* 2 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 20 - 2f */
/* 3 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, AL, /* 30 - 3f */
/* 4 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 40 - 4f */
/* 5 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 50 - 5f */
/* 6 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 60 - 6f */
/* 7 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 70 - 7f */
/* 8 */ __, EX, AL, AL, PR, PO, AL, AL, OP, CL, AL, __, CL, B2, CL, AL, /* 80 - 8f */
/* 9 */ NU, NU, NU, NU, NU, NU, NU, NU, NU, NU, NS, NS, __, __, __, EX, /* 90 - 9f */
/* a */ AL, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* a0 - af */
/* b */ __, __, __, __, __, __, __, __, __, __, __, OP, AL, CL, __, __, /* b0 - bf */
/* c */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* c0 - cf */
/* d */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* d0 - df */
/* e */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* e0 - ef */
/* f */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* f0 - ff */
/*      00  01  02  03  04  05  06  07  08  09  0a  0b  0c  0d  0e  0f */
};

static const char Table_EFBD[256] = {
/*      00  01  02  03  04  05  06  07  08  09  0a  0b  0c  0d  0e  0f */
/* 0 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 00 - 0f */
/* 1 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 10 - 1f */
/* 2 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 20 - 2f */
/* 3 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 30 - 3f */
/* 4 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 40 - 4f */
/* 5 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 50 - 5f */
/* 6 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 60 - 6f */
/* 7 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 70 - 7f */
/* 8 */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* 80 - 8f */
/* 9 */ __, __, __, __, __, __, __, __, __, __, __, OP, __, CL, __, OP, /* 90 - 9f */
/* a */ CL, CL, OP, CL, CL, AL, __, __, __, __, __, __, __, __, __, __, /* a0 - af */
/* b */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, WJ, /* b0 - bf */
/* c */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* c0 - cf */
/* d */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* d0 - df */
/* e */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* e0 - ef */
/* f */ __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, __, /* f0 - ff */
/*      00  01  02  03  04  05  06  07  08  09  0a  0b  0c  0d  0e  0f */
};

#undef __

#define PROHIBITED	LINEBREAK_NOBREAK
#define DIRECT		LINEBREAK_ALLOWBREAK
#define INDIRECT	((char) (~LINEBREAK_NOBREAK & ~LINEBREAK_ALLOWBREAK & 0x7f))

#define X PROHIBITED	/* B ^ A === B SP* × A */
#define i INDIRECT	/* B % A === B × A and B SP+ ÷ A */
#define _ DIRECT	/* B ÷ A */

/* Note that BK, SP will no be used for lookup. */
static const char BrkPairTable[23][23] = {
/*        AI AL B2 BA BB BK CL CP EX GL HY IN IS NS NU OP PO PR QU SP SY WJ ZW */
/* AI */ { X, X, _, i, _, _, X, X, X, i, i, i, X, i, i, i, _, _, i, _, X, X, X }, /* AI */
/* AL */ { i, i, _, i, _, _, X, X, X, i, i, i, X, i, i, i, _, _, i, _, X, X, X }, /* AL */
/* B2 */ { _, _, _, i, _, _, X, X, X, i, i, _, X, i, _, _, _, _, i, _, X, X, X }, /* B2 */
/* BA */ { _, _, _, i, _, _, X, X, X, i, i, _, X, i, _, _, _, _, i, _, X, X, X }, /* BA */
/* BB */ { i, i, i, i, i, _, X, X, X, _, i, i, X, i, i, i, i, i, i, _, X, X, X }, /* BB */
/* BK */ { _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _ }, /* BK */
/* CL */ { _, _, _, i, _, _, X, X, X, i, i, _, X, X, _, _, i, i, i, _, X, X, X }, /* CL */
/* CP */ { i, i, _, i, _, _, X, X, X, i, i, _, X, X, i, _, i, i, i, _, X, X, X }, /* CP */
/* EX */ { _, _, _, i, _, _, X, X, X, i, i, _, X, i, _, _, _, _, i, _, X, X, X }, /* EX */
/* GL */ { i, i, i, i, i, _, X, X, X, i, i, i, X, i, i, i, i, i, i, _, X, X, X }, /* GL */
/* HY */ { _, _, _, i, _, _, X, X, X, _, i, _, X, i, i, _, _, _, i, _, X, X, X }, /* HY */
/* IN */ { _, _, _, i, _, _, X, X, X, i, i, i, X, i, _, _, _, _, i, _, X, X, X }, /* IN */
/* IS */ { i, i, _, i, _, _, X, X, X, i, i, _, X, i, i, _, _, _, i, _, X, X, X }, /* IS */
/* NS */ { _, _, _, i, _, _, X, X, X, i, i, _, X, i, _, _, _, _, i, _, X, X, X }, /* NS */
/* NU */ { i, i, _, i, _, _, X, X, X, i, i, i, X, i, i, i, i, i, i, _, X, X, X }, /* NU */
/* OP */ { X, X, X, X, X, _, X, X, X, X, X, X, X, X, X, X, X, X, X, _, X, X, X }, /* OP */
/* PO */ { i, i, _, i, _, _, X, X, X, i, i, _, X, i, i, i, _, _, i, _, X, X, X }, /* PO */
/* PR */ { _, i, _, i, _, _, X, X, X, i, i, _, X, i, i, i, _, _, i, _, X, X, X }, /* PR */
/* QU */ { i, i, i, i, i, _, X, X, X, i, i, i, X, i, i, X, i, i, i, _, X, X, X }, /* QU */
/* SP */ { _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _ }, /* SP */
/* SY */ { _, _, _, i, _, _, X, X, X, i, i, _, X, i, i, _, _, _, i, _, X, X, X }, /* SY */
/* WJ */ { i, i, i, i, i, _, X, X, X, i, i, i, X, i, i, i, i, i, i, _, X, X, X }, /* WJ */
/* ZW */ { _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, _, X }, /* ZW */
/*        AI AL B2 BA BB BK CL CP EX GL HY IN IS NS NU OP PO PR QU SP SY WJ ZW */
};

#undef _
#undef i
#undef X

/*
 *----------------------------------------------------------------------
 *
 * ComputeBreakLocations --
 *
 *	Compute break locations in UTF-8 text. This function is doing
 *	the same as set_linebreaks_utf8 (from "libunibreak"), but this
 *	function is using a simplified line break algorithm, although
 *	it is following the recommendations at
 *	http://www.unicode.org/reports/tr14/tr14-26.html.
 *
 *	Note that this functions expects that the whole line will be
 *	parsed at once. This interface corresponds to the interface
 *	of the linebreak library. Of course, such a design is a bit
 *	unluckily.
 *
 * Results:
 *	The computed break locations, in 'brks'. This array must be as
 *	large as the input length (specified by 'len').
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
ComputeBreakLocations(
    const unsigned char *text,
    size_t len,
    const char *lang,
    char *brks)
{
    size_t i;
    size_t nbytes;
    size_t nletters;
    size_t brkIndex;
    LBClass cls;
    LBClass prevCls;

    if (len == 0) {
	return;
    }

    i = 0;
    nletters = 0;
    brkIndex = 0;
    cls = BK;
    prevCls = WJ;
    brks[len - 1] = LINEBREAK_MUSTBREAK;

    while (i < len) {
	unsigned char ch;
	LBClass pcls;

	ch = text[i];

	if (ch < 0x80) {
	    pcls = Table_0000[ch];
	    nbytes = 1;
	} else if ((ch & 0xe0) == 0xc0) {
	    pcls = AI;
	    switch (ch) {
	    case 0xc2:
		switch (UCHAR(text[i + 1])) {
		case 0x85: pcls = BK; break; /* NL */
		case 0xac: pcls = AL; break;
		case 0xad: pcls = BA; break;
		case 0xb1: pcls = AL; break;
		case 0xbb: pcls = QU; break;
		}
		break;
	    case 0xc3:
	    case 0xc4:
	    case 0xc5:
	    case 0xc6:
	    case 0xc7:
	    case 0xc8:
	    case 0xc9:
		ch = text[i + 1];
		if (0x80 <= ch && ch <= 0xbf) {
		    pcls = AL;
		}
		break;
	    case 0xca:
		ch = text[i + 1];
		if (0x80 <= ch && ch <= 0xaf) {
		    pcls = AL;
		}
		break;
	    case 0xcb:
		switch (UCHAR(text[i + 1])) {
		case 0x88: /* fallthru */
		case 0x8c: /* fallthru */
		case 0x9f: pcls = BB; break;
		}
		break;
	    case 0xcd:
		if (UCHAR(text[i + 1]) == 0x8f) {
		    pcls = GL;
		}
		break;
	    case 0xd7:
		if (UCHAR(text[i + 1]) == 0x86) {
		    pcls = EX;
		}
		break;
	    case 0xdf:
		if (UCHAR(text[i + 1]) == 0xb8) {
		    pcls = IS;
		}
		break;
	    }
	    nbytes = 2;
	    brks[i] = LINEBREAK_INSIDEACHAR;
	} else if ((ch & 0xf0) == 0xe0) {
	    pcls = AI;
	    switch (ch) {
		case 0xe2:
		    switch (UCHAR(text[i + 1])) {
		    case 0x80: pcls = Table_E280[UCHAR(text[i + 2])]; break;
		    case 0x81: pcls = Table_E281[UCHAR(text[i + 2])]; break;
		    case 0x82: pcls = Table_E282[UCHAR(text[i + 2])]; break;
		    case 0x8c: pcls = Table_E28C[UCHAR(text[i + 2])]; break;
		    case 0x9d: pcls = Table_E29D[UCHAR(text[i + 2])]; break;
		    case 0x9f: pcls = Table_E29F[UCHAR(text[i + 2])]; break;
		    case 0xa6: pcls = Table_E2A6[UCHAR(text[i + 2])]; break;
		    case 0xa7: pcls = Table_E2A7[UCHAR(text[i + 2])]; break;
		    case 0xb8: pcls = Table_E2B8[UCHAR(text[i + 2])]; break;
		    case 0x84:
			switch (UCHAR(text[i + 2])) {
			    case 0x83: /* fallthru */
			    case 0x89: pcls = PO; break;
			    case 0x96: pcls = PR; break;
			}
			break;
		    case 0x88:
			switch (UCHAR(text[i + 2])) {
			    case 0x92: /* fallthru */
			    case 0x93: pcls = PR; break;
			}
			break;
		    case 0xb9:
			switch (UCHAR(text[i + 2])) {
			    case 0x80: pcls = B2; break;
			    case 0x81: pcls = AL; break;
			    case 0x82: pcls = OP; break;
			}
			break;
		    }
		    break;
		case 0xe3:
		    if (UCHAR(text[i + 1]) == 0x80) {
			pcls = Table_E380[UCHAR(text[i + 2])];
		    }
		    break;
		case 0xef:
		    switch (UCHAR(text[i + 1])) {
		    case 0xb8: pcls = Table_EFB8[UCHAR(text[i + 2])]; break;
		    case 0xb9: pcls = Table_EFB9[UCHAR(text[i + 2])]; break;
		    case 0xbc: pcls = Table_EFBC[UCHAR(text[i + 2])]; break;
		    case 0xbd: pcls = Table_EFBD[UCHAR(text[i + 2])]; break;
		    case 0xb4:
			switch (UCHAR(text[i + 2])) {
			    case 0xbe: pcls = CL; break;
			    case 0xbf: pcls = OP; break;
			}
			break;
		    case 0xbb:
			if (UCHAR(text[i + 2]) == 0xbf) {
			    pcls = WJ; /* ZWNBSP (deprecated word joiner) */
			}
			break;
		    case 0xbf:
			switch (UCHAR(text[i + 2])) {
			    case 0xa0: pcls = PO; break;
			    case 0xa1: /* fallthru */
			    case 0xa5: /* fallthru */
			    case 0xa6: pcls = PR; break;
			}
			break;
		    }
		    break;
	    }
	    nbytes = 3;
	    brks[i + 0] = LINEBREAK_INSIDEACHAR;
	    brks[i + 1] = LINEBREAK_INSIDEACHAR;
	} else if ((ch & 0xf8) == 0xf0) {
	    pcls = AI;
	    nbytes = 4;
	    brks[i + 0] = LINEBREAK_INSIDEACHAR;
	    brks[i + 1] = LINEBREAK_INSIDEACHAR;
	    brks[i + 2] = LINEBREAK_INSIDEACHAR;
#if TCL_UTF_MAX > 4
	/*
	 * NOTE: For any reason newer TCL versions will allow > 4 bytes. I cannot
	 * understand this decision, this is not conform to UTF-8 standard.
	 * Moreover this decision is introducing severe compatibility problems.
	 */
	} else if ((ch & 0xf8) == 0xf8) {
	    pcls = AI;
	    nbytes = 5;
	    brks[i + 0] = LINEBREAK_INSIDEACHAR;
	    brks[i + 1] = LINEBREAK_INSIDEACHAR;
	    brks[i + 2] = LINEBREAK_INSIDEACHAR;
	    brks[i + 3] = LINEBREAK_INSIDEACHAR;
# if TCL_UTF_MAX > 5
	} else if ((ch & 0xf8) == 0xfe) {
	    pcls = AI;
	    nbytes = 6;
	    brks[i + 0] = LINEBREAK_INSIDEACHAR;
	    brks[i + 1] = LINEBREAK_INSIDEACHAR;
	    brks[i + 2] = LINEBREAK_INSIDEACHAR;
	    brks[i + 3] = LINEBREAK_INSIDEACHAR;
	    brks[i + 4] = LINEBREAK_INSIDEACHAR;
# endif /*  TCL_UTF_MAX > 5 */
#endif  /*  TCL_UTF_MAX > 4 */
	} else {
	    /*
	     * This fallback is required, because ths current character conversion
	     * algorithm in Tcl library is producing overlong sequences (a violation
	     * of the UTF-8 standard). This observation has been reported to the
	     * Tcl/Tk team, but the response was ignorance.
	     */

	    unsigned k;
	    const char *p = (const char *) text + i;

	    pcls = AI;
	    nbytes = Tcl_UtfNext(p) - p;
	    for (k = 0; k < nbytes; ++k) {
		brks[i + k] = LINEBREAK_INSIDEACHAR;
	    }
	}

	if (i == 0) {
	    if ((cls = pcls) == SP) {
		/* treat SP at start of input as if it followed a WJ */
		prevCls = cls = WJ;
	    }
	} else {
	    switch (pcls) {
	    case BK:
		brks[i - 1] = LINEBREAK_NOBREAK;
		brks[i] = LINEBREAK_MUSTBREAK;
		prevCls = WJ;
		return;
	    case SP:
		/* handle spaces explicitly; do not update cls */
		if (i > 0) {
		    brks[i - 1] = LINEBREAK_NOBREAK;
		    prevCls = SP;
		} else {
		    prevCls = WJ;
		}
		nletters = 0;
		break;
	    case HY: {
		char brk = BrkPairTable[cls][HY];

	    	/*
		 * The hyphen-minus (U+002D) needs special context treatment. For simplicity we
		 * will only check whether we have two preceding, and two succeeding letters.
		 * TODO: Is there a better method for the decision?
		 */

		brks[i - 1] = LINEBREAK_NOBREAK;
		cls = pcls;

		if (brk == INDIRECT) {
		    prevCls = pcls;
		} else {
		    prevCls = WJ;

		    if (brk == LINEBREAK_ALLOWBREAK && nletters >= 2) {
			brkIndex = i - 1;
		    }
		}
		nletters = 0;
	    	break;
	    }
	    default: {
		char brk = BrkPairTable[cls][pcls];

		if (brk == INDIRECT) {
		    brk = (prevCls == SP) ? LINEBREAK_ALLOWBREAK : LINEBREAK_NOBREAK;
		    prevCls = pcls;
		} else {
		    prevCls = WJ;
		}
		brks[i - 1] = brk;
		cls = pcls;

		if (pcls == AL) {
		    nletters += 1;

		    if (brkIndex && nletters >= 2) {
			brks[brkIndex] = LINEBREAK_ALLOWBREAK;
			brkIndex = 0;
		    }
		} else {
		    nletters = 0;
		}
		break;
	    }
	    }
	}

	i += nbytes;
    }
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 105
 * End:
 * vi:set ts=8 sw=4:
 */
