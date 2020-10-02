/*
 * tkMacOSXPort.h --
 *
 *	This file is included by all of the Tk C files.  It contains
 *	information that may be configuration-dependent, such as
 *	#includes for system include files and a few other things.
 *
 * Copyright (c) 1994-1996 Sun Microsystems, Inc.
 * Copyright 2001-2009, Apple Inc.
 * Copyright (c) 2005-2009 Daniel A. Steffen <das@users.sourceforge.net>
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef _TKMACPORT
#define _TKMACPORT

#include <stdio.h>
#include <pwd.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>
#include <math.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/file.h>
#ifdef HAVE_SYS_SELECT_H
#   include <sys/select.h>
#endif
#include <sys/stat.h>
#ifndef _TCL
#   include <tcl.h>
#endif
#if TIME_WITH_SYS_TIME
#   include <sys/time.h>
#   include <time.h>
#else
#   if HAVE_SYS_TIME_H
#	include <sys/time.h>
#   else
#	include <time.h>
#   endif
#endif
#if HAVE_INTTYPES_H
#    include <inttypes.h>
#endif
#include <unistd.h>
#if defined(__GNUC__) && !defined(__cplusplus)
#   pragma GCC diagnostic ignored "-Wc++-compat"
#endif
#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/Xfuncproto.h>
#include <X11/Xutil.h>

/*
 * The following macro defines the type of the mask arguments to
 * select:
 */

#ifndef NO_FD_SET
#   define SELECT_MASK fd_set
#else
#   ifndef _AIX
	typedef long fd_mask;
#   endif
#   if defined(_IBMR2)
#	define SELECT_MASK void
#   else
#	define SELECT_MASK int
#   endif
#endif

/*
 * The following macro defines the number of fd_masks in an fd_set:
 */

#ifndef FD_SETSIZE
#   ifdef OPEN_MAX
#	define FD_SETSIZE OPEN_MAX
#   else
#	define FD_SETSIZE 256
#   endif
#endif
#if !defined(howmany)
#   define howmany(x, y) (((x)+((y)-1))/(y))
#endif
#ifndef NFDBITS
#   define NFDBITS NBBY*sizeof(fd_mask)
#endif
#define MASK_SIZE howmany(FD_SETSIZE, NFDBITS)

/*
 * Define "NBBY" (number of bits per byte) if it's not already defined.
 */

#ifndef NBBY
#   define NBBY 8
#endif

/*
 * The following define causes Tk to use its internal keysym hash table
 */

#define REDO_KEYSYM_LOOKUP

/*
 * The following functions are not used on the Mac, so we stub them out.
 */

#define TkpFreeColor(tkColPtr)
#define TkSetPixmapColormap(p,c) {}

/*
 * Turn off Tk double-buffering as Aqua windows are already double-buffered.
 */

#define TK_NO_DOUBLE_BUFFERING 1
#define TK_HAS_DYNAMIC_COLORS 1
#define TK_DYNAMIC_COLORMAP 0x0fffffff

/*
 * Inform tkImgPhInstance.c that our tkPutImage can render an image with an
 * alpha channel directly into a window.
 */

#define TKPUTIMAGE_CAN_BLEND

/*
 * Used by xcolor.c
 */

MODULE_SCOPE unsigned long TkMacOSXRGBPixel(unsigned long red, unsigned long green,
					    unsigned long blue);
#define TkpGetPixel(p) (TkMacOSXRGBPixel(p->red >> 8, p->green >> 8, p->blue >> 8))

/*
 * Used by tkWindow.c
 */

MODULE_SCOPE void TkMacOSXHandleMapOrUnmap(Tk_Window tkwin, XEvent *event);

#define TkpHandleMapOrUnmap(tkwin, event)  TkMacOSXHandleMapOrUnmap(tkwin, event)

#endif /* _TKMACPORT */
