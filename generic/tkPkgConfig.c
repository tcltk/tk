/*
 * tkPkgConfig.c --
 *
 *	This file contains the configuration information to embed into the tcl
 *	binary library.
 *
 * Copyright © 2002 Andreas Kupries <andreas_kupries@users.sourceforge.net>
 * Copyright © 2017 Stuart Cassoff <stwo@users.sourceforge.net>
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

/* Note, the definitions in this module are influenced by the following C
 * preprocessor macros:
 *
 * - _WIN32 || __CYGWIN__	The value for the fontsytem key will be
 *   MAC_OSX_TK			chosen based on these macros/defines.
 *   HAVE_XFT			declares that xft font support was requested.
 *
 * - CFG_RUNTIME_*		Paths to various stuff at runtime.
 * - CFG_INSTALL_*		Paths to various stuff at installation time.
 *
 * - TCL_CFGVAL_ENCODING	string containing the encoding used for the
 *				configuration values.
 */

#include "tkInt.h"


#ifndef TCL_CFGVAL_ENCODING
#define TCL_CFGVAL_ENCODING "utf-8"
#endif

/*
 * Use C preprocessor statements to define the various values for the embedded
 * configuration information.
 */

#if defined(_WIN32)
#  define CFG_FONTSYSTEM	"gdi"
#elif defined(MAC_OSX_TK)
#  define CFG_FONTSYSTEM	"cocoa"
#elif defined(HAVE_XFT)
#  define CFG_FONTSYSTEM	"xft"
#else
#  define CFG_FONTSYSTEM	"x11"
#endif

static const Tcl_Config cfg[] = {
    {"fontsystem",		CFG_FONTSYSTEM},

    /* Runtime paths to various stuff */

#ifdef CFG_RUNTIME_LIBDIR
    {"libdir,runtime",		CFG_RUNTIME_LIBDIR},
#endif
#ifdef CFG_RUNTIME_BINDIR
    {"bindir,runtime",		CFG_RUNTIME_BINDIR},
#endif
#ifdef CFG_RUNTIME_SCRDIR
    {"scriptdir,runtime",	CFG_RUNTIME_SCRDIR},
#endif
#ifdef CFG_RUNTIME_INCDIR
    {"includedir,runtime",	CFG_RUNTIME_INCDIR},
#endif
#ifdef CFG_RUNTIME_DOCDIR
    {"docdir,runtime",		CFG_RUNTIME_DOCDIR},
#endif
#ifdef CFG_RUNTIME_DEMODIR
    {"demodir,runtime",		CFG_RUNTIME_DEMODIR},
#endif
#if !defined(STATIC_BUILD)
    {"dllfile,runtime",		CFG_RUNTIME_DLLFILE},
#endif

    /* Installation paths to various stuff */

#ifdef CFG_INSTALL_LIBDIR
    {"libdir,install",		CFG_INSTALL_LIBDIR},
#endif
#ifdef CFG_INSTALL_BINDIR
    {"bindir,install",		CFG_INSTALL_BINDIR},
#endif
#ifdef CFG_INSTALL_SCRDIR
    {"scriptdir,install",	CFG_INSTALL_SCRDIR},
#endif
#ifdef CFG_INSTALL_INCDIR
    {"includedir,install",	CFG_INSTALL_INCDIR},
#endif
#ifdef CFG_INSTALL_DOCDIR
    {"docdir,install",		CFG_INSTALL_DOCDIR},
#endif
#ifdef CFG_INSTALL_DEMODIR
    {"demodir,install",		CFG_INSTALL_DEMODIR},
#endif

    /* Last entry, closes the array */
    {NULL, NULL}
};

void
TkInitEmbeddedConfigurationInformation(
    Tcl_Interp *interp)		/* Interpreter the configuration command is
				 * registered in. */
{
    Tcl_RegisterConfig(interp, "tk", cfg, TCL_CFGVAL_ENCODING);
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
