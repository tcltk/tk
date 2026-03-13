/*
 * tkUnixFont.c --
 *
 *	Contains the Unix implementation of the platform-independent font
 *	package interface using Xft for anti-aliased font rendering.
 *
 * Copyright (c) 2003-2008 Joe English
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkInt.h"
#include "tkUnixInt.h"
#include <X11/Xft/Xft.h>
#include "kb.h"
#include "SheenBidi.h"

/*
 * The following structure encapsulates the platform-dependent
 * font data for the Xft font backend.
 */

typedef struct {
    TkFont font;		/* Generic font header.  This must be first! */
    /*
     * Xft-specific data.
     */
    XftFont *xftFont;           /* Primary Xft font */
    XftFont **fallbackFonts;    /* Array of fallback Xft fonts */
    int fallbackCount;          /* Number of fallback fonts */
    TkFontMetrics fm;		/* Cached metrics */
} UnixFont;

/*
 * Xft doesn't provide a way to get this information directly,
 * so we cache it.
 */

typedef struct {
    TkFontMetrics fm;
    int xHeight;
} FontInfo;


/* 
 * Struct for shaping complex script/bidirectional (RTL) text. 
*/
    
typedef struct {
   kb_shaper shaper;
   kb_font *fonts[8];
   int fontCount;

   kb_glyph glyphs[1024];
   int glyphCount;

   int advances[1024];
   int offsetsX[1024];
   int offsetsY[1024];

   int clusters[1024];
} X11Shape;

/*
 * Forward declarations for functions defined in this file.
 */

static void		GetFontInfo(XftFont *xftFont, FontInfo *infoPtr);
static XftFont *	LoadFont(Display *display, Tk_Window tkwin,
			    const TkFontAttributes *faPtr);
static XftFont *	LoadFontFromXLFD(Display *display,
			    const char *xlfd);
void                    X11Shape_Init(X11Shape *s);
void                    X11Shape_AddFont(X11Shape *s, kb_font *f);
int                     X11Shape_Shape(const char *utf8, int len, X11Shape *s);

/*
 *----------------------------------------------------------------------
 *
 * TkpGetNativeFont --
 *
 *	Map a platform-specific native font name to a TkFont.
 *
 * Results:
 *	The return value is a pointer to a TkFont that represents the native
 *	font. If a native font by the given name could not be found, the
 *	return value is NULL.
 *
 * Side effects:
 *	Memory allocated.
 *
 *----------------------------------------------------------------------
 */

TkFont *
TkpGetNativeFont(
    Tk_Window tkwin,		/* For display where font will be used. */
    const char *name)		/* Platform-specific font name. */
{
    Display *display = Tk_Display(tkwin);
    UnixFont *fontPtr;
    XftFont *xftFont;

    xftFont = LoadFontFromXLFD(display, name);
    if (!xftFont) {
	return NULL;
    }

    fontPtr = (UnixFont *)ckalloc(sizeof(UnixFont));
    memset(fontPtr, 0, sizeof(UnixFont));

    fontPtr->xftFont = xftFont;
    GetFontInfo(xftFont, (FontInfo *)&fontPtr->fm);

    return (TkFont *)fontPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpGetFontFromAttributes --
 *
 *	Given a desired set of attributes for a font, find a font with the
 *	closest matching attributes.
 *
 * Results:
 *	The return value is a pointer to a TkFont that represents the font
 *	with the desired attributes.
 *
 * Side effects:
 *	Memory allocated.
 *
 *----------------------------------------------------------------------
 */

TkFont *
TkpGetFontFromAttributes(
    TkFont *tkFontPtr,		/* If non-NULL, store the information in this
				 * existing TkFont structure. */
    Tk_Window tkwin,		/* For display where font will be used. */
    const TkFontAttributes *faPtr)
				/* Set of attributes to match. */
{
    Display *display = Tk_Display(tkwin);
    UnixFont *fontPtr = (UnixFont *)tkFontPtr;
    XftFont *xftFont;

    xftFont = LoadFont(display, tkwin, faPtr);
    if (!xftFont) {
	return NULL;
    }

    if (fontPtr == NULL) {
	fontPtr = (UnixFont *)ckalloc(sizeof(UnixFont));
	memset(fontPtr, 0, sizeof(UnixFont));
    } else {
	/* Free existing font data */
	if (fontPtr->xftFont) {
	    XftFontClose(display, fontPtr->xftFont);
	}
    }

    fontPtr->xftFont = xftFont;
    GetFontInfo(xftFont, (FontInfo *)&fontPtr->fm);

    return (TkFont *)fontPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpDeleteFont --
 *
 *	Called to release a font allocated by TkpGetNativeFont() or
 *	TkpGetFontFromAttributes().
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	TkFont is deallocated.
 *
 *----------------------------------------------------------------------
 */

void
TkpDeleteFont(
    TkFont *tkFontPtr)		/* Token of font to be deleted. */
{
    UnixFont *fontPtr = (UnixFont *)tkFontPtr;

    if (fontPtr->xftFont) {
	XftFontClose(Tk_Display(TkFontDisplay(tkFontPtr)), fontPtr->xftFont);
    }
    ckfree((char *)fontPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_MeasureChars --
 *
 *	Determine the width, in pixels, of a string when displayed in a given
 *	font.
 *
 * Results:
 *	The return value is the width in pixels.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Tk_MeasureChars(
    Tk_Font tkfont,		/* Font in which characters will be drawn. */
    const char *source,		/* UTF-8 string to be displayed. */
    Tcl_Size numBytes,		/* Number of bytes to consider from source. */
    int maxLength,		/* If >=0, maximum line length. */
    int flags,			/* Various flag bits OR-ed together. */
    int *lengthPtr)		/* Filled with x-location just after the
				 * terminating character. */
{
    UnixFont *fontPtr = (UnixFont *) tkfont;
    Display *display = Tk_Display(TkFontDisplay(tkfont));
    X11Shape shape;
    int width = 0;
    int i;

    UnixFontShapeString(fontPtr, source, numBytes, &shape);

    for (i = 0; i < shape.glyphCount; i++) {
        width += shape.advances[i];
    }
    return width;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_TextWidth --
 *
 *	Compute the width, in pixels, of a string when displayed in a given
 *	font.
 *
 * Results:
 *	The return value is the width in pixels.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Tk_TextWidth(
    Tk_Font tkfont,		/* Font in which characters will be drawn. */
    const char *source,		/* UTF-8 string to be displayed. */
    Tcl_Size numBytes)		/* Number of bytes in string. */
{
    return Tk_MeasureChars(tkfont, source, numBytes, -1, 0, NULL);
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_CharBbox --
 *
 *	Given a font and a string, determine the bounding box of the character
 *	at a particular index in the string.
 *
 * Results:
 *	Fills in the x, y, width, and height of the character's bounding box.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
Tk_CharBbox(
    Tk_Font tkfont,		/* Font in which characters will be drawn. */
    const char *source,		/* UTF-8 string to be displayed. */
    Tcl_Size numBytes,		/* Number of bytes in string. */
    Tcl_Size index,		/* Byte offset of character to query. */
    int *xPtr,			/* Return x-coordinate of left edge. */
    int *yPtr,			/* Return y-coordinate of top edge. */
    int *widthPtr,		/* Return width of character. */
    int *heightPtr)		/* Return height of character. */
{
    UnixFont *fontPtr = (UnixFont *) tkfont;
    X11Shape shape;
    int x = 0;
    int w = 0;
    int glyphIdx;
    int i;
    int bytesSoFar = 0;

    /* Shape the string */
    UnixFontShapeString(fontPtr, source, numBytes, &shape);

    /* Find which glyph contains the byte at 'index' */
    glyphIdx = -1;
    for (i = 0; i < shape.glyphCount; i++) {
        if (index >= bytesSoFar && index < bytesSoFar + shape.clusterBytes[i]) {
            glyphIdx = i;
            break;
        }
        bytesSoFar += shape.clusterBytes[i];
    }

    if (glyphIdx < 0 || glyphIdx >= shape.glyphCount) {
        if (xPtr)      *xPtr = 0;
        if (yPtr)      *yPtr = 0;
        if (widthPtr)  *widthPtr = 0;
        if (heightPtr) *heightPtr = 0;
        return;
    }

    /* Calculate x position of the glyph */
    for (i = 0; i < glyphIdx; i++) {
        x += shape.advances[i];
    }

    /* Get width of the glyph */
    w = shape.advances[glyphIdx];

    if (xPtr)      *xPtr = x;
    if (yPtr)      *yPtr = -fontPtr->fm.ascent;
    if (widthPtr)  *widthPtr = w;
    if (heightPtr) *heightPtr = fontPtr->fm.ascent + fontPtr->fm.descent;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_DrawChars --
 *
 *	Draw a string of characters on the screen.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Information gets drawn on the screen.
 *
 *----------------------------------------------------------------------
 */

void
Tk_DrawChars(
    Display *display,		/* Display on which to draw. */
    Drawable drawable,		/* Window or pixmap in which to draw. */
    GC gc,			/* Graphics context for drawing characters. */
    Tk_Font tkfont,		/* Font in which characters will be drawn. */
    const char *source,		/* UTF-8 string to be displayed. */
    Tcl_Size numBytes,		/* Number of bytes in string. */
    int x, int y)		/* Coordinates at which to place origin. */
{
    UnixFont *fontPtr = (UnixFont *) tkfont;
    X11Shape shape;
    XftColor color;
    XRenderColor renderColor;
    XGCValues values;

    /* Extract color from GC for Xft rendering */
    XGetGCValues(display, gc, GCForeground, &values);
    
    renderColor.red = ((values.foreground >> 16) & 0xff) * 0x101;
    renderColor.green = ((values.foreground >> 8) & 0xff) * 0x101;
    renderColor.blue = (values.foreground & 0xff) * 0x101;
    renderColor.alpha = 0xffff;

    XftColorAllocValue(display, 
                       DefaultVisual(display, DefaultScreen(display)),
                       DefaultColormap(display, DefaultScreen(display)),
                       &renderColor, &color);

    UnixFontShapeString(fontPtr, source, numBytes, &shape);

    /* Draw using the shaped glyphs */
    X11Shape_Draw(display, drawable, gc, x, y, &shape, &color);

    XftColorFree(display, 
                 DefaultVisual(display, DefaultScreen(display)),
                 DefaultColormap(display, DefaultScreen(display)),
                 &color);
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_FontId --
 *
 *	Return the platform-specific identifier for a font.
 *
 * Results:
 *	Returns the XftFont pointer cast to Font.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Font
Tk_FontId(
    Tk_Font tkfont)		/* Font to query. */
{
    UnixFont *fontPtr = (UnixFont *) tkfont;
    return (Font) fontPtr->xftFont;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_FontMetrics --
 *
 *	Retrieve the metrics for a font.
 *
 * Results:
 *	Fills in the metrics structure.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
Tk_FontMetrics(
    Tk_Font tkfont,		/* Font to query. */
    TkFontMetrics *fmPtr)	/* Structure to fill in. */
{
    UnixFont *fontPtr = (UnixFont *) tkfont;
    *fmPtr = fontPtr->fm;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpGetFontFamilies --
 *
 *	Return information about the font families that are available.
 *
 * Results:
 *	Modifies interp's result object to hold a list of all the available
 *	font families.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
TkpGetFontFamilies(
    Tcl_Interp *interp,		/* Interp to hold result. */
    Tk_Window tkwin)		/* For display to query. */
{
    Display *display = Tk_Display(tkwin);
    XftFontSet *fs;
    Tcl_HashTable familyTable;
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;
    Tcl_Obj *resultPtr, *strPtr;
    int i;

    Tcl_InitHashTable(&familyTable, TCL_STRING_KEYS);

    fs = XftListFonts(display, DefaultScreen(display),
		      XFT_FAMILY, XFT_TYPE, "pattern",
		      NULL);

    if (fs) {
	for (i = 0; i < fs->nfont; i++) {
	    XftPattern *pat = fs->fonts[i];
	    char *family;
	    
	    if (XftPatternGetString(pat, XFT_FAMILY, 0, &family) == XftResultMatch) {
		int isNew;
		Tcl_CreateHashEntry(&familyTable, family, &isNew);
	    }
	}
	XftFontSetDestroy(fs);
    }

    resultPtr = Tcl_NewObj();
    hPtr = Tcl_FirstHashEntry(&familyTable, &search);
    while (hPtr != NULL) {
	strPtr = Tcl_NewStringObj(Tcl_GetHashKey(&familyTable, hPtr), -1);
	Tcl_ListObjAppendElement(NULL, resultPtr, strPtr);
	hPtr = Tcl_NextHashEntry(&search);
    }
    Tcl_SetObjResult(interp, resultPtr);

    Tcl_DeleteHashTable(&familyTable);
}

/*
 *----------------------------------------------------------------------
 *
 * GetFontInfo --
 *
 *	Retrieve metrics from an XftFont.
 *
 * Results:
 *	Fills in the FontInfo structure.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
GetFontInfo(
    XftFont *xftFont,
    FontInfo *infoPtr)
{
    TkFontMetrics *fmPtr = &infoPtr->fm;
    FcChar8 *str = (FcChar8 *)"x";
    XGlyphInfo extents;

    fmPtr->ascent = xftFont->ascent;
    fmPtr->descent = xftFont->descent;
    fmPtr->maxWidth = xftFont->max_advance_width;

    /* Determine if font is fixed-width */
    fmPtr->fixed = (xftFont->max_advance_width == xftFont->min_advance_width);

    /* Get x-height */
    XftTextExtentsUtf8(xftFont->display, xftFont, str, 1, &extents);
    infoPtr->xHeight = extents.height;
}

/*
 *----------------------------------------------------------------------
 *
 * LoadFont --
 *
 *	Load an Xft font matching the requested attributes.
 *
 * Results:
 *	Returns an XftFont pointer, or NULL on failure.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static XftFont *
LoadFont(
    Display *display,
    Tk_Window tkwin,
    const TkFontAttributes *faPtr)
{
    XftPattern *pat;
    XftPattern *match;
    XftFont *xftFont;
    double size;
    int weight, slant;

    pat = XftPatternCreate();

    if (faPtr->family && *faPtr->family) {
	XftPatternAddString(pat, XFT_FAMILY, faPtr->family);
    }

    size = TkFontGetPixels(tkwin, faPtr->size);
    XftPatternAddDouble(pat, XFT_PIXEL_SIZE, size);

    /* Map Tk weight to Xft weight */
    weight = (faPtr->weight == TK_FW_NORMAL) ? XFT_WEIGHT_MEDIUM : XFT_WEIGHT_BOLD;
    XftPatternAddInteger(pat, XFT_WEIGHT, weight);

    /* Map Tk slant to Xft slant */
    switch (faPtr->slant) {
	case TK_FS_ROMAN:	slant = XFT_SLANT_ROMAN; break;
	case TK_FS_ITALIC:	slant = XFT_SLANT_ITALIC; break;
	case TK_FS_OBLIQUE:	slant = XFT_SLANT_OBLIQUE; break;
	default:		slant = XFT_SLANT_ROMAN;
    }
    XftPatternAddInteger(pat, XFT_SLANT, slant);

    /* Add common patterns */
    XftPatternAddBool(pat, XFT_SCALABLE, FcTrue);
    XftPatternAddBool(pat, XFT_ANTIALIAS, FcTrue);

    match = XftFontMatch(display, DefaultScreen(display), pat, NULL);
    XftPatternDestroy(pat);

    if (!match) {
	return NULL;
    }

    xftFont = XftFontOpenPattern(display, match);
    if (!xftFont) {
	XftPatternDestroy(match);
    }

    return xftFont;
}

/*
 *----------------------------------------------------------------------
 *
 * LoadFontFromXLFD --
 *
 *	Load an Xft font from an XLFD name.
 *
 * Results:
 *	Returns an XftFont pointer, or NULL on failure.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static XftFont *
LoadFontFromXLFD(
    Display *display,
    const char *xlfd)
{
    XftPattern *pat;
    XftPattern *match;
    XftFont *xftFont;

    pat = XftNameParse((FcChar8 *)xlfd);
    if (!pat) {
	return NULL;
    }

    match = XftFontMatch(display, DefaultScreen(display), pat, NULL);
    XftPatternDestroy(pat);

    if (!match) {
	return NULL;
    }

    xftFont = XftFontOpenPattern(display, match);
    if (!xftFont) {
	XftPatternDestroy(match);
    }

    return xftFont;
}


/*
 *----------------------------------------------------------------------
 *
 * Functions to support text shaping and bi-directional rendering.
 *
 *----------------------------------------------------------------------
 */


/* Common shaping helper: primary + fallback fonts. */
static void
UnixFontShapeString(
    UnixFont *fontPtr,
    const char *source,
    int numBytes,
    X11Shape *shapePtr)
{
    int i;

    X11Shape_Init(shapePtr);

    /* Primary font. */
    X11Shape_AddFont(shapePtr, (XftFont *)fontPtr->xftFont);

    /* Fallbacks. */
    for (i = 0; i < fontPtr->fallbackCount; i++) {
        X11Shape_AddFont(shapePtr, fontPtr->fallbackFonts[i]);
    }
    X11Shape_Shape(source, numBytes, shapePtr);
}

/* Initialize shaping context. */
void X11Shape_Init(X11Shape *s)
{
   memset(s, 0, sizeof(*s));
   kb_init(&s->shaper);
}

/* Add a fallback font. */
void X11Shape_AddFont(X11Shape *s, kb_font *f)
{
   s->fonts[s->fontCount++] = f;
}

/* Shape a UTF-8 run. */
int X11Shape_Shape(const char *utf8, int len, X11Shape *s)
{
   kb_run run;
   kb_run_init_utf8(&run, utf8, len);

   kb_shape(&s->shaper, &run, s->fonts, s->fontCount,
            s->glyphs, &s->glyphCount,
            s->advances, s->offsetsX, s->offsetsY,
            s->clusters);
   return 1;
}

/* Draw shaped glyphs via Xft. */
void X11Shape_Draw(Display *dpy, XftDraw *draw,
                                XftColor *color, int x, int y,
                                X11Shape *s)
{
   for (int i = 0; i < s->glyphCount; i++) {
       XftGlyphSpec spec;
       spec.glyph = s->glyphs[i].index;
       spec.x = x + s->offsetsX[i];
       spec.y = y - s->offsetsY[i];
       XftDrawGlyphs(draw, color, s->glyphs[i].font->xft, &spec, 1);
       x += s->advances[i];
   }
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
