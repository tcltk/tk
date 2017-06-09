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

#if HAVE_STDINT_H && (__STDC_VERSION__ >= 199901L)

# include <stdbool.h>

#else /* support of ancient compilers */

# ifdef __cplusplus
#  error "cannot compile with ancient C++ compilers - C99 is required"
# endif

typedef int bool;
enum { true = (bool) 1, false = (bool) 0 };

#endif /* HAVE_STDINT_H */
#endif /* _TK_BOOL */
/* vi:set ts=8 sw=4: */
