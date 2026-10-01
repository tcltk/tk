/*
 * tkTextLineBreak.c --
 *
 *	This module provides line break computation for line wrapping.
 *	It uses the library "libunibreak" (from Wu Yongwei) for the
 *	computation, but only if available (currently only UNIX), and if
 *	the language support is enabled, otherwise mojibake (UAX #14, see
 *	tkTextGrapheme.c).
 *
 * Copyright © 2015-2017 Gregor Cramer
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
 *	is NULL, then mojibake will be used. If 'lang' is not NULL, then
 *	this function tries to load the library "libunibreak" (currently
 *	only UNIX).
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
    return Tcl_LoadFile(interp, pathPtr, symbols, TCL_LOAD_GLOBAL, funcs, handle);
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
    Tcl_Obj *pathPtr = Tcl_NewStringObj("libunibreak.so.1", TCL_INDEX_NONE);
    int rc;

    Tcl_IncrRefCount(pathPtr);
    rc = LoadFile(interp, pathPtr, &handle, Symbols, Funcs);
    if (rc != TCL_OK) {
	/*
	 * We couldn't find "libunibreak.so.1", so try the predecessor "liblinebreak.so.2".
	 */

	Tcl_ResetResult(interp);
	Tcl_DecrRefCount(pathPtr);
	Tcl_IncrRefCount(pathPtr = Tcl_NewStringObj("liblinebreak.so.2", TCL_INDEX_NONE));
	rc = LoadFile(interp, pathPtr, &handle, Symbols, Funcs);
    }
    Tcl_DecrRefCount(pathPtr);
    if (rc == TCL_OK) {
	((InitFunc)(void *)Funcs[0])();
	libLinebreakFunc = (ComputeBreakLocationsFunc)(void *)Funcs[1];
    } else {
	Tcl_ResetResult(interp);
    }
}

#endif /* __UNIX__ */

static ComputeBreakLocationsFunc
GetLineBreakFunc(
    Tcl_Interp *interp,
    char const *lang)
{
#ifdef __UNIX__
    if (lang) {
	static int loaded = 0;

	if (!loaded) {
	    LoadLibUnibreak(interp);
	}
    }
#else
    (void)interp;
    (void)lang;
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
 *	computation, but only if this library is loadable, otherwise
 *	mojibake will be used.
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

int
TkTextComputeBreakLocations(
    Tcl_Interp *interp,
    const char *text,	/* must be nul-terminated */
    unsigned len,	/* without trailing nul byte */
    const char *locale,	/* can be NULL */
    char *brks)
{
    ComputeBreakLocationsFunc func;
    int lastBreakablePos = -1;
    char lang[3] = { 0, 0, 0 };
    unsigned i;

    if (locale && locale[0] && locale[1] && ((locale[2] == '_') || (locale[2] == '\0'))) {
	lang[0] = locale[0];
	lang[1] = locale[1];
    }
    assert(text);
    assert(brks);
    assert(text[len] == '\0');
    assert(!lang[0] || (isalpha(lang[0]) && isalpha(lang[1])));

    func = GetLineBreakFunc(interp, lang[0] ? lang : NULL);

    /*
     * The algorithm don't give us a break value for the last character if we do
     * not include the final nul char into the computation.
     */

    len += 1;
    (*func)((const unsigned char *) text, len, lang[0] ? lang : NULL, brks);
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
		     * The HYPHEN-MINUS (U+002D) needs special context treatment. For simplicity we
		     * will only check whether we have two preceding, and two succeeding letters.
		     * TODO: Is there a better method for the decision?
		     */

		    const char *r = text + i;
		    const char *p, *q, *s;
		    Tcl_UniChar uc;
		    int allow = 0;

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
					    allow = 1;
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
 * Returns whether the text contains a script written without spaces between words
 * (Thai, Lao, Khmer, Myanmar), for which mojibake needs its dictionaries. Tests
 * only the first two bytes of the UTF-8 sequences, a superset is harmless.
 */

static int
HasComplexScript(
    const unsigned char *text,
    size_t len)
{
    size_t i;

    for (i = 0; i + 1 < len; ++i) {
	unsigned char c = text[i + 1];

	switch (text[i]) {
	case 0xe0:	/* U+0E00-U+0EFF: Thai, Lao */
	    if (0xb8 <= c && c <= 0xbb) {
		return 1;
	    }
	    break;
	case 0xe1:	/* U+1000-U+10BF: Myanmar, U+1780-U+17FF, U+19C0-U+19FF: Khmer */
	    if (c <= 0x82 || c == 0x9e || c == 0x9f || c == 0xa7) {
		return 1;
	    }
	    break;
	case 0xea:	/* U+A9C0-U+A9FF, U+AA40-U+AA7F: Myanmar extensions */
	    if (c == 0xa7 || c == 0xa9) {
		return 1;
	    }
	    break;
	}
    }
    return 0;
}

/*
 * The dictionaries are sorted on first use; serialize this initialization.
 */

TCL_DECLARE_MUTEX(dictMutex)

static void
InitDictionaries(void)
{
    static int initialized = 0;

    if (!initialized) {
	Tcl_MutexLock(&dictMutex);
	if (!initialized) {
	    mojibake_dict_init();
	    initialized = 1;
	}
	Tcl_MutexUnlock(&dictMutex);
    }
}

/*
 * Returns whether a mandatory break follows the character p of n bytes: after BK,
 * NL, LF, and CR unless followed by LF (UAX #14 LB4, LB5). mojibake does not report
 * mandatory breaks as break opportunities.
 */

static int
IsMandatoryBreak(
    const unsigned char *p,
    size_t n,
    const unsigned char *end)
{
    switch (n) {
    case 1:
	return *p == '\n' || *p == '\v' || *p == '\f'
		|| (*p == '\r' && (p + 1 == end || p[1] != '\n'));
    case 2:
	return p[0] == 0xc2 && p[1] == 0x85;				/* NEL */
    case 3:
	return p[0] == 0xe2 && p[1] == 0x80 && (p[2] == 0xa8 || p[2] == 0xa9);	/* LS, PS */
    }
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * ComputeBreakLocations --
 *
 *	Compute break locations in UTF-8 text with mojibake: UAX #14, and
 *	dictionary word boundaries for Thai, Lao, Khmer and Myanmar. Same
 *	interface as set_linebreaks_utf8 from "libunibreak": 'len' includes
 *	the trailing nul, the status of the break after a character is stored
 *	at its last byte, its other bytes get LINEBREAK_INSIDEACHAR. Nothing
 *	is known about a break after the last character, unless mandatory.
 *
 * Results:
 *	The computed break locations, in 'brks'. This array must be as
 *	large as 'len'.
 *
 * Side effects:
 *	The dictionaries are sorted on first use.
 *
 *----------------------------------------------------------------------
 */

static void
ComputeBreakLocations(
    const unsigned char *text,
    size_t len,
    TCL_UNUSED(const char *),
    char *brks)
{
    const unsigned char *end;
    unsigned char *breaks;
    size_t i, n;

    if (len == 0) {
	return;
    }

    len -= 1; /* without trailing nul */
    end = text + len;
    breaks = (unsigned char *)Tcl_Alloc(len + 1);

    if (HasComplexScript(text, len)) {
	unsigned char *tmp = (unsigned char *)Tcl_Alloc(2*(len + 1));

	InitDictionaries();
	mojibake_line_breaks_with_dict((const char *) text, len, breaks, tmp, tmp + len + 1);
	Tcl_Free(tmp);
    } else {
	mojibake_line_breaks((const char *) text, len, breaks);
    }

    for (i = 0; i < len; i += n) {
	n = Tcl_UtfNext((const char *) text + i) - ((const char *) text + i);
	if (n == 0 || n > len - i) {
	    n = len - i;
	}
	if (n > 1) {
	    memset(brks + i, LINEBREAK_INSIDEACHAR, n - 1);
	}
	if (IsMandatoryBreak(text + i, n, end)) {
	    brks[i + n - 1] = LINEBREAK_MUSTBREAK;
	} else if (i + n < len && breaks[i + n]) {
	    brks[i + n - 1] = LINEBREAK_ALLOWBREAK;
	} else {
	    brks[i + n - 1] = LINEBREAK_NOBREAK;
	}
    }
    brks[len] = LINEBREAK_MUSTBREAK;
    Tcl_Free(breaks);
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 105
 * End:
 * vi:set ts=8 sw=4:
 */
