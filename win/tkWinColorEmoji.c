/*
 * tkWinColorEmoji.c --
 *
 *	Color glyph rendering for Windows fonts that carry a COLR table (color
 *	emoji).  GDI draws only the outlines of such glyphs; this file is meant
 *	to draw their color layers with DirectWrite/Direct2D, reusing the
 *	glyphs and advances shaped by Uniscribe in tkWinFont.c.
 *
 *	Not implemented yet: TkWinDrawColorGlyphs() reports that nothing was
 *	drawn and the caller falls back to ScriptTextOut().
 *
 * Copyright © 2026 Nicolas Bats
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkWinInt.h"

/*
 *----------------------------------------------------------------------
 *
 * TkWinDrawColorGlyphs --
 *
 *	Draw the color layers of a run of glyphs of the font selected in hdc,
 *	at the baseline origin (x, y), using the Uniscribe glyph indices,
 *	advances and offsets.
 *
 * Results:
 *	1 if the run was drawn, 0 if the caller must draw it with GDI (no
 *	color layers, Direct2D unavailable, or any failure).
 *
 * Side effects:
 *	None yet.
 *
 *----------------------------------------------------------------------
 */

int
TkWinDrawColorGlyphs(
    TCL_UNUSED(HDC),		/* Target device context. */
    TCL_UNUSED(HFONT),		/* Font of the run, selected in the DC. */
    TCL_UNUSED(int),		/* x of the baseline origin. */
    TCL_UNUSED(int),		/* y of the baseline origin. */
    TCL_UNUSED(const SCRIPT_ANALYSIS *),
				/* Uniscribe analysis (direction) of the run. */
    TCL_UNUSED(const WORD *),	/* Glyph indices. */
    TCL_UNUSED(const int *),	/* Advance widths in pixels. */
    TCL_UNUSED(const GOFFSET *),/* Glyph offsets. */
    TCL_UNUSED(int))		/* Number of glyphs. */
{
    return 0;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
