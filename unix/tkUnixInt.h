/*
 * tkUnixInt.h --
 *
 *	This file contains declarations that are shared among the
 *	UNIX-specific parts of Tk but aren't used by the rest of
 *	Tk.
 *
 * Copyright (c) 1995-1997 Sun Microsystems, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * RCS: @(#) $Id: tkUnixInt.h,v 1.4 1999/04/16 01:51:46 stanton Exp $
 */

#ifndef _TKUNIXINT
#define _TKUNIXINT

#ifndef _TKINT
#include "tkInt.h"
#endif

/*
 * Prototypes for procedures that are referenced in files other
 * than the ones they're defined in.
 */
#include "tkIntPlatDecls.h"

#endif /* _TKUNIXINT */
