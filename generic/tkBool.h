/*
 * tkBool.h --
 *
 *	This module provides a boolean type, conform to C++.
 *
 * Copyright (c) 2015-2017 Gregor Cramer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef _TK_BOOL
#define _TK_BOOL

#ifdef HAVE_STDBOOL_H

# include <stdbool.h>

#else /* support of ancient compilers */

# include "../compat/stdbool.h"

#endif /* HAVE_STDBOOL_H */
#endif /* _TK_BOOL */
/* vi:set ts=8 sw=4: */
