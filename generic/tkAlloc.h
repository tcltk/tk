/*
 * tkAlloc.h --
 *
 *	This module provides an interface to memory allocation functions, this
 *	is: malloc(), realloc(), free(). This has the following advantages:
 *
 *	1. The whole features of the very valuable tool Valgrind can be used,
 *	   this requires to bypass the Tcl allocation functions.
 *
 *	2. Backport to version 8.5, this is important because the Mac version
 *	   of wish8.6 has event loop issues.
 *
 * Copyright (c) 2015-2017 Gregor Cramer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef _TK_ALLOC
#define _TK_ALLOC

#include <tcl.h>
#include <tclDecls.h> /* needed for the Tcl_Alloc macros */
#include <stdlib.h>


#if TK_VALGRIND /* ===========================================================*/

/*
 * Ensure that the Tcl allocation suite will not be used, because a mix
 * of ckalloc and malloc must be avoided. When valgrind mode is activated
 * then the functions malloc/realloc/free will be used directly, in this
 * way the fine granulated memory check of valgrind can be used. This is
 * not possible when using the Tcl allocation suite, because these
 * functions are bypassing the fine granulated check. (Valgrind is replacing
 * malloc/realloc/free with his own memory functions.)
 */

# undef ckalloc
# undef ckrealloc
# undef ckfree

# ifdef Tcl_Alloc
#  undef Tcl_Alloc
#  undef Tcl_Realloc
#  undef Tcl_Free
#  undef Tcl_AttemptAlloc
#  undef Tcl_AttemptRealloc
#  undef Tcl_DbCkalloc
#  undef Tcl_DbCkrealloc
#  undef Tcl_DbCkfree
# endif

# define Tcl_Alloc --
# define Tcl_Realloc --
# define Tcl_Free --
# define Tcl_AttemptAlloc --
# define Tcl_AttemptRealloc --
# define Tcl_DbCkalloc --
# define Tcl_DbCkrealloc --
# define Tcl_DbCkfree --

#else /* if !TK_VALGRIND ==================================================== */

/*
 * If valgrind mode is disabled, then we use the Tcl allocations functions.
 * This means that malloc/realloc/free are simply wrappers to the Tcl
 * functions ckalloc/ckrealloc/ckfree.
 */

/* the main reason for these definitions is portability to 8.5 */
# define malloc(size)		((void *) (ckalloc(size)))
# define realloc(ptr, size)	((void *) (ckrealloc((char *) (ptr), size)))
# define free(ptr)		ckfree((char *) (ptr))

#endif /* TK_VALGRIND ======================================================= */


/*
 * The following functions/macros are not supported with this memory
 * allocation scheme.
 */

# undef attemptckalloc
# undef attemptckrealloc

#endif /* _TK_ALLOC */
/* vi:set ts=8 sw=4: */
