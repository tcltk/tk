/*
 *  tkMacOSXImage.h --
 *
 *
 *	The code in this file provides an interface for XImages, and
 *      implements the nsimage image type.
 *
 * Copyright © 1995-1997 Sun Microsystems, Inc.
 * Copyright © 2001-2009 Apple Inc.
 * Copyright © 2005-2009 Daniel A. Steffen <das@users.sourceforge.net>
 * Copyright © 2017-2021 Marc Culler.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

/*
 * Function prototypes
 */

MODULE_SCOPE CFDataRef CreatePDFFromDrawableRect( Drawable drawable,
	   int x, int y, unsigned int width, unsigned int height);
