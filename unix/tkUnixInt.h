/*
 * tkUnixInt.h --
 *
 *	This file contains declarations that are shared among the
 *	UNIX-specific parts of Tk but aren't used by the rest of Tk.
 *
 * Copyright (c) 1995-1997 Sun Microsystems, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef _TKUNIXINT
#define _TKUNIXINT

#ifndef _TKINT
#include "tkInt.h"
#endif

/*
 * Prototypes for procedures that are referenced in files other than the ones
 * they're defined in.
 */

#include "tkIntPlatDecls.h"

/*
 * Platform specific extension of the XKeyEvent struct which appends a
 * character string to be used for the %A percent replacement. 
 */

#define XMaxTransChars 7

typedef struct {
    XKeyEvent keyEvent;		/* The real event from X11. */
    char *charValuePtr;		/* A pointer to a string that holds the key's
				 * %A substitution text (before backslash
				 * adding), or NULL if that has not been
				 * computed yet. If non-NULL, this string was
				 * allocated with ckalloc(). */
    int charValueLen;		/* Length of string in charValuePtr when that
				 * is non-NULL. */
    KeySym keysym;		/* Key symbol computed after input methods
				 * have been invoked */
} TkKeyEvent;

#endif /* _TKUNIXINT */

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
