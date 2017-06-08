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
#endif

typedef int tkbool_t;

#ifndef __cplusplus
enum { true = (tkbool_t) 1, false = (tkbool_t) 0 };
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* _TK_BOOL */
/* vi:set ts=8 sw=4: */
