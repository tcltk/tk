/*
 * tkPort.h --
 *
 *	This header file handles porting issues that occur because of
 *	differences between systems.  It reads in platform specific
 *	portability files.
 *
 * Copyright (c) 1995 Sun Microsystems, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef _TKPORT
#define _TKPORT

#if defined(PLATFORM_SDL)
#   include "tkSDLPort.h"
#endif
#if defined(_WIN32)
#   if !defined(PLATFORM_SDL)
#	include "tkWinPort.h"
#   endif
#endif
#ifndef _TK
#   include "tk.h"
#endif
#if !defined(_WIN32)
#   if defined(MAC_OSX_TK)
#	include "tkMacOSXPort.h"
#   else
#       if !defined(PLATFORM_SDL)
#	    include "tkUnixPort.h"
#       endif
#   endif
#endif

#endif /* _TKPORT */
