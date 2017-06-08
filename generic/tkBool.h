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

#if 0 /* TODO: we need a different solution */
#ifdef __cplusplus
extern "C" {
#endif
#endif

/* does not work on all platforms...
typedef int bool;
*/

/* ...so we need a different approach: */
#define bool int

#ifndef __cplusplus
enum { true = (bool) 1, false = (bool) 0 };
#endif

#if 0 /* TODO: we need a different solution */
#ifdef __cplusplus
} /* extern "C" */
#endif
#endif

#endif /* _TK_BOOL */
/* vi:set ts=8 sw=4: */
