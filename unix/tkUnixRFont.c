#include "tkUnixInt.h"
#include "tkFont.h"
#include <X11/Xft/Xft.h>
#include <ctype.h>

typedef struct _UnixFtFace {
    XftFont	    *ftFont;
    FcPattern	    *source;
    FcCharSet	    *charset;
} UnixFtFace;

typedef struct _UnixFtFont {
    TkFont	    font;    	/* Stuff used by generic font package.  Must
				 * be first in structure. */
    UnixFtFace	    *faces;
    int		    nfaces;
    FcCharSet	    *charset;
    FcPattern	    *pattern;
    
    Display	    *display;
    int		    screen;
    XftDraw	    *ftDraw;
    Drawable	    drawable;
    XftColor	    color;
} UnixFtFont;

void
TkpFontPkgInit(mainPtr)
    TkMainInfo *mainPtr;	/* The application being created. */
{
}

static XftFont *
GetFont (UnixFtFont *fontPtr, FcChar32 ucs4)
{
    int	    i;

    if (ucs4)
    {
	for (i = 0; i < fontPtr->nfaces; i++)
	{
	    FcCharSet   *charset = fontPtr->faces[i].charset;
	    if (charset && FcCharSetHasChar (charset, ucs4))
		break;
	}
	if (i == fontPtr->nfaces)
	    i = 0;
    }
    else
	i = 0;
    if (!fontPtr->faces[i].ftFont)
    {
	FcPattern   *pat = FcFontRenderPrepare (0, fontPtr->pattern,
						fontPtr->faces[i].source);
						
	fontPtr->faces[i].ftFont = XftFontOpenPattern (fontPtr->display,
						       pat);
    }
    return fontPtr->faces[i].ftFont;
}

static UnixFtFont *
InitFont (Tk_Window tkwin, FcPattern *pattern)
{
    UnixFtFont		*fontPtr;
    TkFontAttributes	*faPtr;
    TkFontMetrics	*fmPtr;
    char		*family;
    int			weight, slant;
    double		size;
    int			spacing;
    FcFontSet		*set;
    FcCharSet		*charset;
    FcResult		result;
    int			i;
    XftFont		*ftFont;
    
    fontPtr = (UnixFtFont *) ckalloc (sizeof (UnixFtFont));
    if (!fontPtr)
	return NULL;
    
    FcConfigSubstitute (0, pattern, FcMatchPattern);
    XftDefaultSubstitute (Tk_Display (tkwin),
			  Tk_ScreenNumber (tkwin),
			  pattern);

    /*
     * Generate the list of fonts
     */
    set = FcFontSort (0, pattern, FcTrue, &charset, &result);

    if (!set)
    {
	FcPatternDestroy (pattern);
	ckfree ((char *) fontPtr);
	return NULL;
    }

    fontPtr->charset = charset;
    fontPtr->pattern = pattern;
    
    fontPtr->faces = (UnixFtFace *) ckalloc (set->nfont * sizeof (UnixFtFace));
    if (!fontPtr->faces)
    {
	FcFontSetDestroy (set);
	FcCharSetDestroy (charset);
	FcPatternDestroy (pattern);
	ckfree ((char *) fontPtr);
	return NULL;
    }
    fontPtr->nfaces = set->nfont;
    
    /*
     * Fill in information about each returned font
     */
    for (i = 0; i < set->nfont; i++)
    {
	fontPtr->faces[i].ftFont = 0;
	fontPtr->faces[i].source = set->fonts[i];
	if (FcPatternGetCharSet (set->fonts[i], FC_CHARSET, 0, &charset) == FcResultMatch)
	    fontPtr->faces[i].charset = FcCharSetCopy (charset);
	else
	    fontPtr->faces[i].charset = 0;
    }

    /*
     * Build the Tk font structure
     */
    if (XftPatternGetString (pattern, XFT_FAMILY, 0, &family) != XftResultMatch)
	family = "Unknown";
    
    if (XftPatternGetInteger (pattern, XFT_WEIGHT, 0, &weight) != XftResultMatch)
	weight = XFT_WEIGHT_MEDIUM;
    if (weight <= XFT_WEIGHT_MEDIUM)
	weight = TK_FW_NORMAL;
    else
	weight = TK_FW_BOLD;
    
    if (XftPatternGetInteger (pattern, XFT_SLANT, 0, &slant) != XftResultMatch)
	slant = XFT_SLANT_ROMAN;
    if (slant <= XFT_SLANT_ROMAN)
	slant = TK_FS_ROMAN;
    else
	slant = TK_FS_ITALIC;
    
    if (XftPatternGetDouble (pattern, XFT_SIZE, 0, &size) != XftResultMatch)
	size = 12.0;
    
    if (XftPatternGetInteger (pattern, XFT_SPACING, 0, &spacing) != XftResultMatch)
	spacing = XFT_PROPORTIONAL;
    if (spacing == XFT_PROPORTIONAL)
	spacing = 0;
    else
	spacing = 1;
#if 0
    printf ("family %s size %g weight %d slant %d\n", family, size, weight, slant);
#endif
    fontPtr->font.fid	= XLoadFont (Tk_Display (tkwin), "fixed");
    
    fontPtr->display	= Tk_Display (tkwin);
    fontPtr->screen	= Tk_ScreenNumber (tkwin);
    fontPtr->ftDraw	= 0;
    fontPtr->drawable	= 0;
    fontPtr->color.color.red  = 0;
    fontPtr->color.color.green= 0;
    fontPtr->color.color.blue = 0;
    fontPtr->color.color.alpha= 0xffff;
    fontPtr->color.pixel = 0xffffffff;
    
    faPtr		= &fontPtr->font.fa;
    faPtr->family	= family;
    faPtr->size		= (int) size;
    faPtr->weight	= weight;
    faPtr->slant	= slant;
    faPtr->underline	= 0;
    faPtr->overstrike	= 0;

    ftFont              = GetFont (fontPtr, 0);
    fmPtr		= &fontPtr->font.fm;
    fmPtr->ascent	= ftFont->ascent;
    fmPtr->descent	= ftFont->descent;
    fmPtr->maxWidth	= ftFont->max_advance_width;
    fmPtr->fixed	= spacing;

    return fontPtr;
}

static void
FiniFont (UnixFtFont *fontPtr)
{
    Display	    *display = fontPtr->display;
    Tk_ErrorHandler handler;
    int		    i;
    
    handler = Tk_CreateErrorHandler(display, -1, -1, -1,
				    (Tk_ErrorProc *) NULL, (ClientData) NULL);
    for (i = 0; i < fontPtr->nfaces; i++)
    {
	if (fontPtr->faces[i].ftFont)
	    XftFontClose (fontPtr->display, fontPtr->faces[i].ftFont);
	if (fontPtr->faces[i].source)
	    FcPatternDestroy (fontPtr->faces[i].source);
	if (fontPtr->faces[i].charset)
	    FcCharSetDestroy (fontPtr->faces[i].charset);
    }
    if (fontPtr->ftDraw)
	XftDrawDestroy (fontPtr->ftDraw);
    if (fontPtr->font.fid)
	XUnloadFont (fontPtr->display, fontPtr->font.fid);
    Tk_DeleteErrorHandler(handler);
}

TkFont *
TkpGetNativeFont(tkwin, name)
    Tk_Window tkwin;		/* For display where font will be used. */
    CONST char *name;		/* Platform-specific font name. */
{
    UnixFtFont	*fontPtr;
    FcPattern	*pattern;
#if 0
    printf ("TkpGetNativeFont %s\n", name);
#endif
    if (*name == '-')
	pattern = XftXlfdParse (name, FcFalse, FcFalse);
    else
	pattern = FcNameParse (name);
#if 0
    XftPatternPrint (pattern);
#endif
    fontPtr = InitFont (tkwin, pattern);
    if (!fontPtr)
	return NULL;
    return &fontPtr->font;
}

TkFont *
TkpGetFontFromAttributes(tkFontPtr, tkwin, faPtr)
    TkFont *tkFontPtr;		/* If non-NULL, store the information in
				 * this existing TkFont structure, rather than
				 * allocating a new structure to hold the
				 * font; the existing contents of the font
				 * will be released.  If NULL, a new TkFont
				 * structure is allocated. */
    Tk_Window tkwin;		/* For display where font will be used. */
    CONST TkFontAttributes *faPtr;
				/* Set of attributes to match. */
{
    XftPattern	*pattern;
    int		weight, slant;
    UnixFtFont	*fontPtr;

#if 0
    printf ("TkpGetFontFromAttributes %s-%d %d %d\n", faPtr->family,
	    faPtr->size, faPtr->weight, faPtr->slant);
#endif
    pattern = XftPatternBuild (0,
			       XFT_FAMILY, XftTypeString, faPtr->family,
			       0);
    if (faPtr->size > 0)
	XftPatternAddInteger (pattern, XFT_SIZE, faPtr->size);
    else
	XftPatternAddInteger (pattern, XFT_PIXEL_SIZE, -faPtr->size);
    switch (faPtr->weight) {
    case TK_FW_NORMAL:
    default:
	weight = XFT_WEIGHT_MEDIUM;
	break;
    case TK_FW_BOLD:
	weight = XFT_WEIGHT_BOLD;
	break;
    }
    XftPatternAddInteger (pattern, XFT_WEIGHT, weight);
    switch (faPtr->slant) {
    case TK_FS_ROMAN:
    default:
	slant = XFT_SLANT_ROMAN;
	break;
    case TK_FS_ITALIC:
	slant = XFT_SLANT_ITALIC;
	break;
    case TK_FS_OBLIQUE:
	slant = XFT_SLANT_OBLIQUE;
	break;
    }
    XftPatternAddInteger (pattern, XFT_SLANT, slant);

    fontPtr = InitFont (tkwin, pattern);
    if (!fontPtr)
	return NULL;
    return &fontPtr->font;
}

void
TkpDeleteFont(tkFontPtr)
    TkFont *tkFontPtr;		/* Token of font to be deleted. */
{
    UnixFtFont	*fontPtr = (UnixFtFont *) tkFontPtr;

    FiniFont (fontPtr);
    /* XXX tkUnixFont.c doesn't free tkFontPtr... */
}

void
TkpGetFontFamilies(interp, tkwin)
    Tcl_Interp *interp;		/* Interp to hold result. */
    Tk_Window tkwin;		/* For display to query. */
{
    Tcl_Obj	*resultPtr, *strPtr;
    XftFontSet	*list;
    int		i;
    char	*family;

    resultPtr = Tcl_GetObjResult(interp);    

    list = XftListFonts (Tk_Display (tkwin),
			 Tk_ScreenNumber (tkwin),
			 0,
			 XFT_FAMILY,
			 0);
    for (i = 0; i < list->nfont; i++)
    {
	if (XftPatternGetString (list->fonts[i], XFT_FAMILY, 0, &family) == XftResultMatch)
	{
	    strPtr = Tcl_NewStringObj(Tk_GetUid (family), -1);
	    Tcl_ListObjAppendElement (NULL, resultPtr, strPtr);
	}
    }
    XftFontSetDestroy (list);
}

void
TkpGetSubFonts(interp, tkfont)
    Tcl_Interp *interp;
    Tk_Font tkfont;
{
    Tcl_Obj	*objv[3];
    Tcl_Obj	*resultPtr, *listPtr;
    UnixFtFont	*fontPtr = (UnixFtFont *) tkfont;
    char	*family, *foundry, *encoding;
    XftFont	*ftFont = GetFont (fontPtr, 0);
    
    resultPtr = Tcl_GetObjResult(interp);    
    if (XftPatternGetString (ftFont->pattern, XFT_FAMILY, 
			     0, &family) != XftResultMatch)
    {
	family = "Unknown";
    }
    if (XftPatternGetString (ftFont->pattern, XFT_FOUNDRY,
			     0, &foundry) != XftResultMatch)
    {
	foundry = "Unknown";
    }
    if (XftPatternGetString (ftFont->pattern, XFT_ENCODING,
			     0, &encoding))
    {
	encoding = "Unknown";
    }
    objv[0] = Tcl_NewStringObj(family, -1);
    objv[1] = Tcl_NewStringObj(foundry, -1);
    objv[2] = Tcl_NewStringObj(encoding, -1);
    listPtr = Tcl_NewListObj (3, objv);
    Tcl_ListObjAppendElement (NULL, resultPtr, listPtr);
}
int
Tk_MeasureChars(tkfont, source, numBytes, maxLength, flags, lengthPtr)
    Tk_Font tkfont;		/* Font in which characters will be drawn. */
    CONST char *source;		/* UTF-8 string to be displayed.  Need not be
				 * '\0' terminated. */
    int numBytes;		/* Maximum number of bytes to consider
				 * from source string. */
    int maxLength;		/* If >= 0, maxLength specifies the longest
				 * permissible line length in pixels; don't
				 * consider any character that would cross
				 * this x-position.  If < 0, then line length
				 * is unbounded and the flags argument is
				 * ignored. */
    int flags;			/* Various flag bits OR-ed together:
				 * TK_PARTIAL_OK means include the last char
				 * which only partially fit on this line.
				 * TK_WHOLE_WORDS means stop on a word
				 * boundary, if possible.
				 * TK_AT_LEAST_ONE means return at least one
				 * character even if no characters fit. */
    int *lengthPtr;		/* Filled with x-location just after the
				 * terminating character. */
{
    UnixFtFont	    *fontPtr = (UnixFtFont *) tkfont;
    XftFont	    *ftFont;
    FcChar32	    c;
    int		    clen;
    XGlyphInfo	    extents;
    int		    curX, newX;
    int		    termByte = 0;
    int		    termX = 0;
    int		    curByte, newByte;
    int		    sawNonSpace;
#if 0
    char	    string[256];
    int		    len = 0;
#endif
    
    curX = 0;
    curByte = 0;
    sawNonSpace = 0;
    while (numBytes > 0)
    {
	clen = FcUtf8ToUcs4 ((FcChar8 *) source, &c, numBytes);
	source += clen;
	numBytes -= clen;
	if (c < 256 && isspace (c))
	{
	    if (sawNonSpace)
	    {
		termByte = curByte;
		termX = curX;
		sawNonSpace = 0;
	    }
	}
	else
	    sawNonSpace = 1;

#if 0
	string[len++] = (char) c;
#endif
	ftFont = GetFont (fontPtr, c);

	XftTextExtents32 (fontPtr->display, ftFont, &c, 1, &extents);

	newX    = curX + extents.xOff;
	newByte = curByte + clen;
	if (maxLength >= 0 && newX > maxLength)
	{
	    if ((flags & TK_PARTIAL_OK) ||
		((flags & TK_AT_LEAST_ONE) && curByte == 0))
	    {
		curX = newX;
		curByte = newByte;
	    } 
	    else if (flags & TK_WHOLE_WORDS)
	    {
		curX = termX;
		curByte = termByte;
	    }
	    break;
	}
	
	curX     = newX;
	curByte  = newByte;
    }
#if 0
    string[len] = '\0';
    printf ("MeasureChars %s length %d bytes %d\n", string, curX, curByte);
#endif
    *lengthPtr = curX;
    return curByte;
}

#define NUM_SPEC    1024

void
Tk_DrawChars(display, drawable, gc, tkfont, source, numBytes, x, y)
    Display *display;		/* Display on which to draw. */
    Drawable drawable;		/* Window or pixmap in which to draw. */
    GC gc;			/* Graphics context for drawing characters. */
    Tk_Font tkfont;		/* Font in which characters will be drawn;
				 * must be the same as font used in GC. */
    CONST char *source;		/* UTF-8 string to be displayed.  Need not be
				 * '\0' terminated.  All Tk meta-characters
				 * (tabs, control characters, and newlines)
				 * should be stripped out of the string that
				 * is passed to this function.  If they are
				 * not stripped out, they will be displayed as
				 * regular printing characters. */
    int numBytes;		/* Number of bytes in string. */
    int x, y;			/* Coordinates at which to place origin of
				 * string when drawing. */
{
    UnixFtFont		*fontPtr = (UnixFtFont *) tkfont;
    XGCValues		values;
    XColor		xcolor;
    int			clen;
    XftGlyphFontSpec	specs[NUM_SPEC];
    int			nspec;
    XGlyphInfo		metrics;

    if (fontPtr->ftDraw == 0)
    {
#if 0
	printf ("Switch to drawable 0x%x\n", drawable);
#endif
	fontPtr->ftDraw = XftDrawCreate (display,
					 drawable,
					 DefaultVisual (display,
							fontPtr->screen),
					 DefaultColormap (display,
							  fontPtr->screen));
	fontPtr->drawable = drawable;
    }
    else
    {
	Tk_ErrorHandler handler;

        handler = Tk_CreateErrorHandler(display, -1, -1, -1,
					(Tk_ErrorProc *) NULL, (ClientData) NULL);
	XftDrawChange (fontPtr->ftDraw, drawable);
	fontPtr->drawable = drawable;
	Tk_DeleteErrorHandler(handler);
    }
    XGetGCValues (display, gc, GCForeground, &values);
    if (values.foreground != fontPtr->color.pixel)
    {
	xcolor.pixel = values.foreground;
	XQueryColor (display, DefaultColormap (display, 
					       fontPtr->screen),
		     &xcolor);
	fontPtr->color.color.red = xcolor.red;
	fontPtr->color.color.green = xcolor.green;
	fontPtr->color.color.blue = xcolor.blue;
	fontPtr->color.color.alpha = 0xffff;
	fontPtr->color.pixel = values.foreground;
    }
    nspec = 0;
    while (numBytes > 0)
    {
	XftFont	    *ftFont;
	FcChar32    c;
	
	clen = FcUtf8ToUcs4 ((FcChar8 *) source, &c, numBytes);
	source += clen;
	numBytes -= clen;

	ftFont = GetFont (fontPtr, c);
	if (ftFont)
	{
	    specs[nspec].font = ftFont;
	    specs[nspec].glyph = XftCharIndex (fontPtr->display, ftFont, c);
	    specs[nspec].x = x;
	    specs[nspec].y = y;

	    XftGlyphExtents (fontPtr->display, ftFont, &specs[nspec].glyph,
			     1, &metrics);
	    x += metrics.xOff;
	    y += metrics.yOff;
	    nspec++;
	    if (nspec == NUM_SPEC)
	    {
		XftDrawGlyphFontSpec (fontPtr->ftDraw, &fontPtr->color,
				      specs, nspec);
		nspec = 0;
	    }
	}
    }
    if (nspec)
	XftDrawGlyphFontSpec (fontPtr->ftDraw, &fontPtr->color,
			      specs, nspec);
}
