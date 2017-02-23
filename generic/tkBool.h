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

#ifdef __cplusplus
extern "C" {
# define bool TkBool
#endif

typedef int bool;

#ifndef __cplusplus
enum { true = (int) 1, false = (int) 0 };
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif


/*
 * For the text widget stuff (and related files) this is a basic header
 * file, so it's the appropriate place for the C99 inline support macros.
 */

#ifdef _MSC_VER
# if defined(include)
#  define TK_C99_INLINE_SUPPORT
# elif _MSC_VER >= 1400
#  define inline __inline
#  define TK_C99_INLINE_SUPPORT
#  define TK_C99_INLINE_DEFINED
# else
#  define inline
#  define TK_C99_INLINE_DEFINED
# endif
#elif __STDC_VERSION__ >= 199901L
# define TK_C99_INLINE_SUPPORT
#else
# define inline
# define TK_C99_INLINE_DEFINED
#endif

#endif /* _TK_BOOL */
/* vi:set ts=8 sw=4: */
