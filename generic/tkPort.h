/*
 * tkPort.h --
 *
 *	This header file handles porting issues that occur because of
 *	differences between systems.  It reads in platform specific
 *	portability files.
 *
 * Copyright © 1995 Sun Microsystems, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef _TKPORT
#define _TKPORT

#ifndef _TK
#   include "tk.h"
#endif
#if defined(_WIN32)
#   include "tkWinPort.h"
#elif defined(MAC_OSX_TK)
#   include "tkMacOSXPort.h"
#elif defined(HAVE_WAYLAND)
#   include "tkWaylandPort.h"
#else
#   include "tkUnixPort.h"
#endif

#endif /* _TKPORT */
