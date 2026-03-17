/*
 * tkUnixRFont.c --
 *
 *	Alternate implementation of tkUnixFont.c using Xft. Supports text 
 *  shaping and bidirectional rendering for complex text and RTL languages 
 *  via external tkUnixShaper API.
 *
 * Copyright (c) 2002-2003 Keith Packard
 * Copyright (c) 2026 Kevin Walzer
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkUnixInt.h"
#include "tkFont.h"
#include <X11/Xft/Xft.h>
#include <math.h>
#include <stdlib.h>

#include "tkUnixShaper.h"   /* All bidi + shaping is now here */

#define MAX_CACHED_COLORS 16
#define TK_DRAW_IN_CONTEXT
#define NUM_SPEC 1024

/*
 * Debugging support...
 */
#define DEBUG_FONTSEL 0
#define DEBUG(arguments) \
    if (DEBUG_FONTSEL) { \
	printf arguments; fflush(stdout); \
    }

/*
 * One UnixFtFace per fontconfig face (main + fallbacks).
 */
typedef struct {
    XftFont *ftFont;       /* rotated variant (if angle != 0) */
    XftFont *ft0Font;      /* upright (angle == 0) */
    FcPattern *source;
    FcCharSet *charset;
    double angle;
} UnixFtFace;

/*
 * Cached XftColor entries (avoids repeated XQueryColor round-trips).
 */
typedef struct {
    XftColor color;
    int next;
} UnixFtColorList;

/*
 * The per-font structure.
 */
typedef struct {
    TkFont font;		/* Stuff used by generic font package. Must be first. */
    UnixFtFace *faces;
    int nfaces;
    FcFontSet *fontset;
    FcPattern *pattern;

    Display *display;
    int screen;
    Colormap colormap;
    Visual *visual;
    XftDraw *ftDraw;
    int ncolors;
    int firstColor;
    UnixFtColorList colors[MAX_CACHED_COLORS];
    X11Shape shape;		/* opaque to this file — managed by tkUnixShaper */
} UnixFtFont;

/*
 * Used to describe the current clipping box. Can't be passed normally because
 * the information isn't retrievable from the GC.
 */
typedef struct {
    Region clipRegion;		/* The clipping region, or None */
} ThreadSpecificData;
static Tcl_ThreadDataKey dataKey;

TCL_DECLARE_MUTEX(xftMutex);
#define LOCK Tcl_MutexLock(&xftMutex)
#define UNLOCK Tcl_MutexUnlock(&xftMutex)

/*
 * Forward declarations
 */
static XftFont *GetFont(UnixFtFont *fontPtr, FcChar32 ucs4, double angle);
static void GetTkFontAttributes(Tk_Window tkwin, XftFont *ftFont,
				TkFontAttributes *faPtr);
static void GetTkFontMetrics(XftFont *ftFont, TkFontMetrics *fmPtr);
static XftFont *GetFontForFace(UnixFtFont *fontPtr, int faceIndex, double angle);
static XftColor *LookUpColor(Display *display, UnixFtFont *fontPtr,
			     unsigned long pixel);
static void UnixFontDrawShapedText(Display *display, Drawable drawable, GC gc,
				   UnixFtFont *fontPtr, const char *source,
				   int numBytes, double originX, double originY,
				   double angle_deg);
static int InitFontErrorProc(void *clientData, XErrorEvent *errPtr);

/*
 *----------------------------------------------------------------------
 *
 * TkpFontPkgInit --
 *
 *	This procedure is called when an application is created. It
 *	initializes all the structures that are used by the
 *	platform-dependant code on a per application basis.
 *	Note that this is called before TkpInit() !
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static Tcl_Size utf8ToUcs4(const char *source, FcChar32 *c, Tcl_Size numBytes)
{
    if (numBytes >= 6) {
	return Tcl_UtfToUniChar(source, (int *)c);
    }
    return FcUtf8ToUcs4((const FcChar8 *)source, c, numBytes);
}

void
TkpFontPkgInit(
    TCL_UNUSED(TkMainInfo *))	/* The application being created. */
{
}

/*
 *----------------------------------------------------------------------
 *
 * GetFont --
 *
 *	Returns an XftFont for the given character and angle, opening it if
 *	necessary. Uses ft0Font for upright text, ftFont for rotated.
 *
 * Results:
 *	An XftFont *, or panics on total failure.
 *
 * Side effects:
 *	May open a new font.
 *
 *----------------------------------------------------------------------
 */

static XftFont *
GetFont(UnixFtFont *fontPtr, FcChar32 ucs4, double angle)
{
    int i;

    if (ucs4) {
	for (i = 0; i < fontPtr->nfaces; i++) {
	    FcCharSet *charset = fontPtr->faces[i].charset;
	    if (charset && FcCharSetHasChar(charset, ucs4)) {
		break;
	    }
	}
	if (i == fontPtr->nfaces) {
	    i = 0;
	}
    } else {
	i = 0;
    }

    if ((angle == 0.0 && !fontPtr->faces[i].ft0Font) ||
	(angle != 0.0 && (!fontPtr->faces[i].ftFont ||
			  fontPtr->faces[i].angle != angle))) {

	FcPattern *pat = FcFontRenderPrepare(NULL, fontPtr->pattern,
					      fontPtr->faces[i].source);
	double s = sin(angle * M_PI / 180.0);
	double c = cos(angle * M_PI / 180.0);
	FcMatrix mat;
	mat.xx = mat.yy = c;
	mat.xy = -mat.yx = s;

	if (angle != 0.0) {
	    FcPatternAddMatrix(pat, FC_MATRIX, &mat);
	}

	LOCK;
	XftFont *ftFont = XftFontOpenPattern(fontPtr->display, pat);
	UNLOCK;

	if (!ftFont) {
	    /* Fallback */
	    LOCK;
	    ftFont = XftFontOpen(fontPtr->display, fontPtr->screen,
				 XFT_FAMILY, FcTypeString, "sans",
				 XFT_SIZE, FcTypeDouble, 12.0,
				 XFT_MATRIX, FcTypeMatrix, &mat,
				 NULL);
	    UNLOCK;
	}
	if (!ftFont) {
	    Tcl_Panic("Cannot find a usable font");
	}

	if (angle == 0.0) {
	    fontPtr->faces[i].ft0Font = ftFont;
	} else {
	    if (fontPtr->faces[i].ftFont) {
		LOCK;
		XftFontClose(fontPtr->display, fontPtr->faces[i].ftFont);
		UNLOCK;
	    }
	    fontPtr->faces[i].ftFont = ftFont;
	    fontPtr->faces[i].angle = angle;
	}
    }

    return (angle == 0.0 ? fontPtr->faces[i].ft0Font : fontPtr->faces[i].ftFont);
}

/*
 *----------------------------------------------------------------------
 *
 * GetTkFontAttributes --
 *
 *	Fill TkFontAttributes from XftFont.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Fills the provided TkFontAttributes structure.
 *
 *----------------------------------------------------------------------
 */

static void
GetTkFontAttributes(Tk_Window tkwin, XftFont *ftFont, TkFontAttributes *faPtr)
{
    const char *family = "Unknown";
    const char *const *familyPtr = &family;
    double ptSize, dblPxSize, size;
    int intPxSize, weight, slant;

    (void) XftPatternGetString(ftFont->pattern, XFT_FAMILY, 0, familyPtr);

    if (XftPatternGetDouble(ftFont->pattern, XFT_SIZE, 0, &ptSize) == XftResultMatch) {
	size = ptSize;
    } else if (XftPatternGetDouble(ftFont->pattern, XFT_PIXEL_SIZE, 0, &dblPxSize) == XftResultMatch) {
	size = -dblPxSize;
    } else if (XftPatternGetInteger(ftFont->pattern, XFT_PIXEL_SIZE, 0, &intPxSize) == XftResultMatch) {
	size = (double)-intPxSize;
    } else {
	size = 12.0;
    }

    if (XftPatternGetInteger(ftFont->pattern, XFT_WEIGHT, 0, &weight) != XftResultMatch) {
	weight = XFT_WEIGHT_MEDIUM;
    }
    if (XftPatternGetInteger(ftFont->pattern, XFT_SLANT, 0, &slant) != XftResultMatch) {
	slant = XFT_SLANT_ROMAN;
    }

    faPtr->family = Tk_GetUid(family);
    faPtr->size = TkFontGetPoints(tkwin, size);
    faPtr->weight = (weight > XFT_WEIGHT_MEDIUM) ? TK_FW_BOLD : TK_FW_NORMAL;
    faPtr->slant = (slant > XFT_SLANT_ROMAN) ? TK_FS_ITALIC : TK_FS_ROMAN;
    faPtr->underline = 0;
    faPtr->overstrike = 0;
}

/*
 *----------------------------------------------------------------------
 *
 * GetTkFontMetrics --
 *
 *	Fill TkFontMetrics from XftFont.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Fills the provided TkFontMetrics structure.
 *
 *----------------------------------------------------------------------
 */

static void
GetTkFontMetrics(XftFont *ftFont, TkFontMetrics *fmPtr)
{
    int spacing;

    if (XftPatternGetInteger(ftFont->pattern, XFT_SPACING, 0, &spacing) != XftResultMatch) {
	spacing = XFT_PROPORTIONAL;
    }

    fmPtr->ascent = ftFont->ascent;
    fmPtr->descent = ftFont->descent;
    fmPtr->maxWidth = ftFont->max_advance_width;
    fmPtr->fixed = spacing != XFT_PROPORTIONAL;
}

/*
 *----------------------------------------------------------------------
 *
 * InitFont --
 *
 *	Initialize a UnixFtFont structure.
 *
 * Results:
 *	Returns fontPtr on success, NULL on failure (frees fontPtr if allocated).
 *
 * Side effects:
 *	Opens fonts, sets metrics, initializes shaper state.
 *
 *----------------------------------------------------------------------
 */

static UnixFtFont *
InitFont(Tk_Window tkwin, FcPattern *pattern, UnixFtFont *fontPtr)
{
    FcFontSet *set;
    FcCharSet *charset;
    FcResult result;
    XftFont *ftFont;
    int i, iWidth, errorFlag;
    Tk_ErrorHandler handler;

    if (!fontPtr) {
	fontPtr = (UnixFtFont *)Tcl_Alloc(sizeof(UnixFtFont));
    }

    FcConfigSubstitute(NULL, pattern, FcMatchPattern);
    XftDefaultSubstitute(Tk_Display(tkwin), Tk_ScreenNumber(tkwin), pattern);

    set = FcFontSort(NULL, pattern, FcTrue, NULL, &result);
    if (!set || set->nfont == 0) {
	Tcl_Free(fontPtr);
	return NULL;
    }

    fontPtr->fontset = set;
    fontPtr->pattern = pattern;
    fontPtr->faces = (UnixFtFace *)Tcl_Alloc(set->nfont * sizeof(UnixFtFace));
    fontPtr->nfaces = set->nfont;

    for (i = 0; i < set->nfont; i++) {
	fontPtr->faces[i].ftFont = NULL;
	fontPtr->faces[i].ft0Font = NULL;
	fontPtr->faces[i].source = set->fonts[i];
	if (FcPatternGetCharSet(set->fonts[i], FC_CHARSET, 0, &charset) == FcResultMatch) {
	    fontPtr->faces[i].charset = FcCharSetCopy(charset);
	} else {
	    fontPtr->faces[i].charset = NULL;
	}
	fontPtr->faces[i].angle = 0.0;
    }

    fontPtr->display = Tk_Display(tkwin);
    fontPtr->screen = Tk_ScreenNumber(tkwin);
    fontPtr->colormap = Tk_Colormap(tkwin);
    fontPtr->visual = Tk_Visual(tkwin);
    fontPtr->ftDraw = NULL;
    fontPtr->ncolors = 0;
    fontPtr->firstColor = -1;

    /* Shaping state is initialized lazily by tkUnixShaper on first use. */
    X11Shape_Init(&fontPtr->shape);

    errorFlag = 0;
    handler = Tk_CreateErrorHandler(Tk_Display(tkwin), -1, -1, -1,
				    InitFontErrorProc, (void *)&errorFlag);
    ftFont = GetFont(fontPtr, 0, 0.0);
    if (!ftFont || errorFlag) {
	Tk_DeleteErrorHandler(handler);
	FinishedWithFont(fontPtr);
	Tcl_Free(fontPtr);
	return NULL;
    }

    fontPtr->font.fid = XLoadFont(Tk_Display(tkwin), "fixed");
    GetTkFontAttributes(tkwin, ftFont, &fontPtr->font.fa);
    GetTkFontMetrics(ftFont, &fontPtr->font.fm);
    Tk_DeleteErrorHandler(handler);

    if (errorFlag) {
	FinishedWithFont(fontPtr);
	Tcl_Free(fontPtr);
	return NULL;
    }

    /* Underline / overstrike defaults. */
    {
	TkFont *fPtr = &fontPtr->font;
	fPtr->underlinePos = fPtr->fm.descent / 2;
	handler = Tk_CreateErrorHandler(Tk_Display(tkwin), -1, -1, -1,
					InitFontErrorProc, (void *)&errorFlag);
	errorFlag = 0;
	Tk_MeasureChars((Tk_Font)fPtr, "I", 1, -1, 0, &iWidth);
	Tk_DeleteErrorHandler(handler);
	if (errorFlag) {
	    FinishedWithFont(fontPtr);
	    Tcl_Free(fontPtr);
	    return NULL;
	}
	fPtr->underlineHeight = iWidth / 3;
	if (fPtr->underlineHeight == 0) {
	    fPtr->underlineHeight = 1;
	}
	if (fPtr->underlineHeight + fPtr->underlinePos > fPtr->fm.descent) {
	    fPtr->underlineHeight = fPtr->fm.descent - fPtr->underlinePos;
	    if (fPtr->underlineHeight == 0) {
		fPtr->underlinePos--;
		fPtr->underlineHeight = 1;
	    }
	}
    }

    return fontPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * FinishedWithFont --
 *
 *	Clean up a UnixFtFont.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees all resources associated with the font.
 *
 *----------------------------------------------------------------------
 */

static void
FinishedWithFont(UnixFtFont *fontPtr)
{
    Display *display = fontPtr->display;
    int i;
    Tk_ErrorHandler handler = Tk_CreateErrorHandler(display, -1, -1, -1, NULL, NULL);

    for (i = 0; i < fontPtr->nfaces; i++) {
	if (fontPtr->faces[i].ftFont) {
	    LOCK;
	    XftFontClose(display, fontPtr->faces[i].ftFont);
	    UNLOCK;
	}
	if (fontPtr->faces[i].ft0Font) {
	    LOCK;
	    XftFontClose(display, fontPtr->faces[i].ft0Font);
	    UNLOCK;
	}
	if (fontPtr->faces[i].charset) {
	    FcCharSetDestroy(fontPtr->faces[i].charset);
	}
    }
    if (fontPtr->faces) {
	Tcl_Free(fontPtr->faces);
    }
    if (fontPtr->pattern) {
	FcPatternDestroy(fontPtr->pattern);
    }
    if (fontPtr->ftDraw) {
	XftDrawDestroy(fontPtr->ftDraw);
    }
    if (fontPtr->font.fid) {
	XUnloadFont(display, fontPtr->font.fid);
    }
    if (fontPtr->fontset) {
	FcFontSetDestroy(fontPtr->fontset);
    }

    /* Clean up shaper state. */
    Tk_ShaperDestroy((Tk_Font)&fontPtr->font);

    Tk_DeleteErrorHandler(handler);
}

/*
 *----------------------------------------------------------------------
 *
 * TkpGetNativeFont --
 *
 *	Platform-specific font creation from XLFD name.
 *
 * Results:
 *	Returns a TkFont handle, or NULL on failure.
 *
 * Side effects:
 *	Creates and initializes a new font.
 *
 *----------------------------------------------------------------------
 */

TkFont *
TkpGetNativeFont(Tk_Window tkwin, const char *name)
{
    FcPattern *pattern;
    UnixFtFont *fontPtr;

    DEBUG(("TkpGetNativeFont: %s\n", name));

    pattern = XftXlfdParse(name, FcFalse, FcFalse);
    if (!pattern) {
	return NULL;
    }

    fontPtr = InitFont(tkwin, pattern, NULL);
    if (!fontPtr) {
	FcPatternDestroy(pattern);
	return NULL;
    }
    return &fontPtr->font;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpGetFontFromAttributes --
 *
 *	Create or update font from attributes.
 *
 * Results:
 *	Returns a TkFont handle, or NULL on failure.
 *
 * Side effects:
 *	Creates a new font or updates an existing one.
 *
 *----------------------------------------------------------------------
 */

TkFont *
TkpGetFontFromAttributes(TkFont *tkFontPtr, Tk_Window tkwin,
			 const TkFontAttributes *faPtr)
{
    FcPattern *pattern;
    int weight, slant;
    UnixFtFont *fontPtr;

    DEBUG(("TkpGetFontFromAttributes: %s %ld %d %d\n",
	   faPtr->family, lround(faPtr->size), faPtr->weight, faPtr->slant));

    pattern = FcPatternCreate();
    if (faPtr->family) {
	FcPatternAddString(pattern, FC_FAMILY, (FcChar8 *)faPtr->family);
    }
    if (faPtr->size > 0.0) {
	FcPatternAddDouble(pattern, FC_SIZE, faPtr->size);
    } else if (faPtr->size < 0.0) {
	FcPatternAddDouble(pattern, FC_SIZE, TkFontGetPoints(tkwin, faPtr->size));
    } else {
	FcPatternAddDouble(pattern, FC_SIZE, 12.0);
    }

    switch (faPtr->weight) {
    case TK_FW_NORMAL:
    default:
	weight = XFT_WEIGHT_MEDIUM;
	break;
    case TK_FW_BOLD:
	weight = XFT_WEIGHT_BOLD;
	break;
    }
    FcPatternAddInteger(pattern, FC_WEIGHT, weight);

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
    FcPatternAddInteger(pattern, FC_SLANT, slant);

    fontPtr = (UnixFtFont *)tkFontPtr;
    if (fontPtr) {
	FinishedWithFont(fontPtr);
    }
    fontPtr = InitFont(tkwin, pattern, fontPtr);

    if (!fontPtr) {
	/* Try non-rendered fallback. */
	FcPatternAddBool(pattern, FC_RENDER, FcFalse);
	fontPtr = InitFont(tkwin, pattern, fontPtr);
    }

    if (!fontPtr) {
	FcPatternDestroy(pattern);
	return NULL;
    }

    fontPtr->font.fa.underline = faPtr->underline;
    fontPtr->font.fa.overstrike = faPtr->overstrike;
    return &fontPtr->font;
}

/*
 *----------------------------------------------------------------------
 *
 * TkpDeleteFont --
 *
 *	Delete font.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees all resources associated with the font.
 *
 *----------------------------------------------------------------------
 */

void
TkpDeleteFont(TkFont *tkFontPtr)
{
    UnixFtFont *fontPtr = (UnixFtFont *)tkFontPtr;
    FinishedWithFont(fontPtr);
    /* tkUnixFont.c historically didn't free tkFontPtr here */
}

/*
 *----------------------------------------------------------------------
 *
 * TkpGetFontFamilies --
 *
 *	List available font families.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets the interpreter result to a list of font family names.
 *
 *----------------------------------------------------------------------
 */

void
TkpGetFontFamilies(Tcl_Interp *interp, Tk_Window tkwin)
{
    XftFontSet *list;
    int i;
    Tcl_Obj *resultPtr = Tcl_NewListObj(0, NULL);

    list = XftListFonts(Tk_Display(tkwin), Tk_ScreenNumber(tkwin),
			NULL, XFT_FAMILY, NULL);

    for (i = 0; i < list->nfont; i++) {
	const char *family;
	const char *const *familyPtr = &family;
	if (XftPatternGetString(list->fonts[i], XFT_FAMILY, 0, familyPtr) == XftResultMatch) {
	    Tcl_ListObjAppendElement(NULL, resultPtr, Tcl_NewStringObj(family, -1));
	}
    }
    XftFontSetDestroy(list);
    Tcl_SetObjResult(interp, resultPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * TkpGetSubFonts --
 *
 *	Return sub-font information (for testfont command).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets the interpreter result to a list of sub-font information.
 *
 *----------------------------------------------------------------------
 */

void
TkpGetSubFonts(Tcl_Interp *interp, Tk_Font tkfont)
{
    UnixFtFont *fontPtr = (UnixFtFont *)tkfont;
    Tcl_Obj *resultPtr = Tcl_NewListObj(0, NULL);
    int i;

    for (i = 0; i < fontPtr->nfaces; i++) {
	FcPattern *pat = FcFontRenderPrepare(NULL, fontPtr->pattern,
					     fontPtr->faces[i].source);
	const char *family = "Unknown", *foundry = "Unknown", *encoding = "Unknown";
	const char *const *ptr;

	ptr = &family;
	XftPatternGetString(pat, XFT_FAMILY, 0, ptr);
	Tcl_Obj *objv[3] = {
	    Tcl_NewStringObj(family, -1),
	    Tcl_NewStringObj(foundry, -1),
	    Tcl_NewStringObj(encoding, -1)
	};
	Tcl_ListObjAppendElement(NULL, resultPtr,
				 Tcl_NewListObj(3, objv));
    }
    Tcl_SetObjResult(interp, resultPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * TkpGetFontAttrsForChar --
 *
 *	Get attributes of font used for a specific character.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Fills the provided TkFontAttributes structure.
 *
 *----------------------------------------------------------------------
 */
void
TkpGetFontAttrsForChar(Tk_Window tkwin, Tk_Font tkfont, int c,
		       TkFontAttributes *faPtr)
{
    UnixFtFont *fontPtr = (UnixFtFont *)tkfont;
    XftFont *ftFont = GetFont(fontPtr, (FcChar32)c, 0.0);
    GetTkFontAttributes(tkwin, ftFont, faPtr);
    faPtr->underline = fontPtr->font.fa.underline;
    faPtr->overstrike = fontPtr->font.fa.overstrike;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_MeasureChars --
 *
 *	Measure how much of a string fits in a given width.
 *
 * Results:
 *	Returns the number of bytes that fit within the given width.
 *
 * Side effects:
 *	Sets *lengthPtr to the consumed width.
 *
 *----------------------------------------------------------------------
 */
int
Tk_MeasureChars(Tk_Font tkfont, const char *source, Tcl_Size numBytes,
		int maxLength, int flags, int *lengthPtr)
{
    UnixFtFont *fontPtr = (UnixFtFont *)tkfont;

    TkShapedTextBuffer *shaped = Tk_ShapeText(tkfont, source, (int)numBytes);
    if (!shaped || shaped->glyphCount == 0) {
	*lengthPtr = 0;
	return 0;
    }

    int total = 0;
    int i;
    for (i = 0; i < shaped->glyphCount; i++) {
	int next = total + shaped->glyphs[i].advanceX;
	if (maxLength >= 0 && next > maxLength) {
	    if (flags & TK_PARTIAL_OK) {
		total = next;
	    } else if ((flags & TK_AT_LEAST_ONE) && total == 0) {
		total = next;
	    }
	    break;
	}
	total = next;
    }
    *lengthPtr = total;

    /* Approximate byte count (original heuristic kept) */
    if (total >= shaped->totalWidth) {
	return (int)numBytes;
    } else {
	int byteCount = (int)(numBytes * (double)total / (shaped->totalWidth + 1) + 0.5);
	if (byteCount < 1 && (flags & TK_AT_LEAST_ONE)) byteCount = 1;
	if (byteCount > numBytes) byteCount = (int)numBytes;
	return byteCount;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_MeasureCharsInContext --
 *
 *	Measure substring.
 *
 * Results:
 *	Returns the number of bytes that fit within the given width.
 *
 * Side effects:
 *	Sets *lengthPtr to the consumed width.
 *
 *----------------------------------------------------------------------
 */
int
Tk_MeasureCharsInContext(Tk_Font tkfont, const char *source,
			 TCL_UNUSED(Tcl_Size), Tcl_Size rangeStart,
			 Tcl_Size rangeLength, int maxLength, int flags,
			 int *lengthPtr)
{
    return Tk_MeasureChars(tkfont, source + rangeStart, rangeLength,
			   maxLength, flags, lengthPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * LookUpColor --
 *
 *	Get XftColor from pixel value (with small LRU cache).
 *
 * Results:
 *	Returns a pointer to an XftColor structure.
 *
 * Side effects:
 *	May update the color cache.
 *
 *----------------------------------------------------------------------
 */
static XftColor *
LookUpColor(Display *display, UnixFtFont *fontPtr, unsigned long pixel)
{
    int i, last = -1, last2 = -1;
    XColor xcolor;

    for (i = fontPtr->firstColor; i >= 0; last2 = last, last = i,
	 i = fontPtr->colors[i].next) {
	if (pixel == fontPtr->colors[i].color.pixel) {
	    if (last >= 0) {
		fontPtr->colors[last].next = fontPtr->colors[i].next;
		fontPtr->colors[i].next = fontPtr->firstColor;
		fontPtr->firstColor = i;
	    }
	    return &fontPtr->colors[i].color;
	}
    }

    if (fontPtr->ncolors < MAX_CACHED_COLORS) {
	last2 = -1;
	last = fontPtr->ncolors++;
    }

    xcolor.pixel = pixel;
    XQueryColor(display, fontPtr->colormap, &xcolor);

    fontPtr->colors[last].color.color.red   = xcolor.red;
    fontPtr->colors[last].color.color.green = xcolor.green;
    fontPtr->colors[last].color.color.blue  = xcolor.blue;
    fontPtr->colors[last].color.color.alpha = 0xFFFF;
    fontPtr->colors[last].color.pixel = pixel;

    if (last2 >= 0) {
	fontPtr->colors[last2].next = fontPtr->colors[last].next;
    }
    fontPtr->colors[last].next = fontPtr->firstColor;
    fontPtr->firstColor = last;

    return &fontPtr->colors[last].color;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_DrawChars --
 *
 *	Draw text (horizontal, no rotation).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Renders text to the specified drawable.
 *
 *----------------------------------------------------------------------
 */
void
Tk_DrawChars(Display *display, Drawable drawable, GC gc, Tk_Font tkfont,
	     const char *source, Tcl_Size numBytes, int x, int y)
{
    UnixFtFont *fontPtr = (UnixFtFont *)tkfont;
    UnixFontDrawShapedText(display, drawable, gc, fontPtr,
			   source, (int)numBytes, (double)x, (double)y, 0.0);
}

/*
 *----------------------------------------------------------------------
 *
 * TkDrawAngledChars --
 *
 *	Draw rotated text.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Renders rotated text to the specified drawable.
 *
 *----------------------------------------------------------------------
 */

void
TkDrawAngledChars(Display *display, Drawable drawable, GC gc, Tk_Font tkfont,
		  const char *source, Tcl_Size numBytes,
		  double x, double y, double angle)
{
    UnixFtFont *fontPtr = (UnixFtFont *)tkfont;
    UnixFontDrawShapedText(display, drawable, gc, fontPtr,
			   source, (int)numBytes, x, y, angle);
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_DrawCharsInContext --
 *
 *	Draw substring (coordinates are for whole line).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Renders a substring of text to the specified drawable.
 *
 *----------------------------------------------------------------------
 */

void
Tk_DrawCharsInContext(Display *display, Drawable drawable, GC gc,
		      Tk_Font tkfont, const char *source,
		      TCL_UNUSED(Tcl_Size), Tcl_Size rangeStart,
		      Tcl_Size rangeLength, int x, int y)
{
    UnixFtFont *fontPtr = (UnixFtFont *)tkfont;
    TkShapedTextBuffer *shaped = Tk_ShapeText(tkfont, source,
					      (int)(rangeStart + rangeLength));
    if (!shaped || shaped->glyphCount == 0) {
	return;
    }

    if (fontPtr->ftDraw == NULL) {
	fontPtr->ftDraw = XftDrawCreate(display, drawable,
					fontPtr->visual, fontPtr->colormap);
    } else {
	Tk_ErrorHandler handler = Tk_CreateErrorHandler(display, -1, -1, -1, NULL, NULL);
	XftDrawChange(fontPtr->ftDraw, drawable);
	Tk_DeleteErrorHandler(handler);
    }

    XGCValues values;
    XftColor *xftcolor;
    XGetGCValues(display, gc, GCForeground, &values);
    xftcolor = LookUpColor(display, fontPtr, values.foreground);

    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
    if (tsdPtr->clipRegion) {
	XftDrawSetClip(fontPtr->ftDraw, tsdPtr->clipRegion);
    }

    XftGlyphFontSpec specs[NUM_SPEC];
    int nspec = 0;
    XftFont *prevFont = NULL;

    Tcl_Size glyphStart = rangeStart;
    Tcl_Size glyphEnd = rangeStart + rangeLength;
    if (glyphEnd > shaped->glyphCount) glyphEnd = shaped->glyphCount;

    int i;
    for (i = (int)glyphStart; i < (int)glyphEnd; i++) {
	XftFont *ftFont = shaped->glyphs[i].xftFont;
	if (!ftFont) ftFont = fontPtr->faces[0].ft0Font;
	if (!ftFont) continue;

	if (ftFont != prevFont && nspec > 0) {
	    LOCK;
	    XftDrawGlyphFontSpec(fontPtr->ftDraw, xftcolor, specs, nspec);
	    UNLOCK;
	    nspec = 0;
	}
	prevFont = ftFont;

	specs[nspec].font  = ftFont;
	specs[nspec].glyph = shaped->glyphs[i].glyphId;
	specs[nspec].x     = x + shaped->glyphs[i].x;
	specs[nspec].y     = y + shaped->glyphs[i].y;

	if (++nspec >= NUM_SPEC) {
	    LOCK;
	    XftDrawGlyphFontSpec(fontPtr->ftDraw, xftcolor, specs, nspec);
	    UNLOCK;
	    nspec = 0;
	    prevFont = NULL;
	}
    }

    if (nspec > 0) {
	LOCK;
	XftDrawGlyphFontSpec(fontPtr->ftDraw, xftcolor, specs, nspec);
	UNLOCK;
    }

    if (tsdPtr->clipRegion) {
	XftDrawSetClip(fontPtr->ftDraw, NULL);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TkpDrawAngledCharsInContext --
 *
 *	Draw rotated substring (coordinates for whole line).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Renders rotated substring to the specified drawable.
 *
 *----------------------------------------------------------------------
 */
void
TkpDrawAngledCharsInContext(Display *display, Drawable drawable, GC gc,
			    Tk_Font tkfont, const char *source,
			    TCL_UNUSED(Tcl_Size), Tcl_Size rangeStart,
			    Tcl_Size rangeLength,
			    double x, double y, double angle)
{
    UnixFtFont *fontPtr = (UnixFtFont *)tkfont;
    TkShapedTextBuffer *shaped = Tk_ShapeText(tkfont, source,
					      (int)(rangeStart + rangeLength));
    if (!shaped || shaped->glyphCount == 0) {
	return;
    }

    double offsetX = 0.0;
    int i;
    for (i = 0; i < shaped->glyphCount; i++) {
	if (offsetX >= rangeStart) break;
	offsetX += shaped->glyphs[i].advanceX;
    }

    double rad = angle * M_PI / 180.0;
    double cosA = cos(rad);
    double sinA = sin(rad);

    double drawX = x + offsetX * cosA;
    double drawY = y - offsetX * sinA;

    UnixFontDrawShapedText(display, drawable, gc, fontPtr,
			   source + rangeStart, (int)rangeLength,
			   drawX, drawY, angle);
}

/*
 *----------------------------------------------------------------------
 *
 * TkUnixSetXftClipRegion --
 *
 *	Set thread-local clip region.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the thread-local clipping region.
 *
 *----------------------------------------------------------------------
 */
void
TkUnixSetXftClipRegion(Region clipRegion)
{
    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
    tsdPtr->clipRegion = clipRegion;
}

/*
 *----------------------------------------------------------------------
 *
 * Tk_DrawCharsRotated --
 *
 *	Draw rotated text (public API).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Renders rotated text to the specified drawable.
 *
 *----------------------------------------------------------------------
 */
void
Tk_DrawCharsRotated(Display *display, Drawable drawable, GC gc,
		    Tk_Font tkfont, const char *source, int numBytes,
		    int x, int y, double angle)
{
    UnixFtFont *fontPtr = (UnixFtFont *)tkfont;
    UnixFontDrawShapedText(display, drawable, gc, fontPtr,
			   source, numBytes, (double)x, (double)y, angle);
}

/*
 *----------------------------------------------------------------------
 *
 * UnixFontDrawShapedText --
 *
 *	Unified drawing function for shaped text (horizontal or rotated).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Renders shaped text to the specified drawable.
 *
 *----------------------------------------------------------------------
 */

static void
UnixFontDrawShapedText(Display *display, Drawable drawable, GC gc,
		       UnixFtFont *fontPtr, const char *source, int numBytes,
		       double originX, double originY, double angle_deg)
{
    TkShapedTextBuffer *shaped = Tk_ShapeText((Tk_Font)fontPtr, source, numBytes);
    if (!shaped || shaped->glyphCount == 0) {
	return;
    }

    if (fontPtr->ftDraw == NULL) {
	fontPtr->ftDraw = XftDrawCreate(display, drawable,
					fontPtr->visual, fontPtr->colormap);
    } else {
	Tk_ErrorHandler handler = Tk_CreateErrorHandler(display, -1, -1, -1, NULL, NULL);
	XftDrawChange(fontPtr->ftDraw, drawable);
	Tk_DeleteErrorHandler(handler);
    }

    XGCValues values;
    XftColor *xftcolor;
    XGetGCValues(display, gc, GCForeground, &values);
    xftcolor = LookUpColor(display, fontPtr, values.foreground);

    ThreadSpecificData *tsdPtr = (ThreadSpecificData *)
	Tcl_GetThreadData(&dataKey, sizeof(ThreadSpecificData));
    if (tsdPtr->clipRegion) {
	XftDrawSetClip(fontPtr->ftDraw, tsdPtr->clipRegion);
    }

    double rad  = (angle_deg != 0.0) ? angle_deg * M_PI / 180.0 : 0.0;
    double cosA = cos(rad);
    double sinA = sin(rad);

    XftGlyphFontSpec specs[NUM_SPEC];
    int nspec = 0;
    XftFont *lastFont = NULL;

    int penX = 0, penY = 0;

    int i;
    for (i = 0; i < shaped->glyphCount; i++) {
	XftFont *drawFont = shaped->glyphs[i].xftFont;

	if (angle_deg != 0.0) {
	    /* For rotated text we need to look up the angled face. */
	    int faceIndex = 0;
	    for (int j = 0; j < fontPtr->shape.numFonts; j++) {
		if (fontPtr->shape.fontMap[j].kbFont == shaped->glyphs[i].kbFont) {
		    faceIndex = fontPtr->shape.fontMap[j].faceIndex;
		    break;
		}
	    }
	    drawFont = GetFontForFace(fontPtr, faceIndex, angle_deg);
	    if (!drawFont) drawFont = GetFont(fontPtr, 0, angle_deg);
	}

	if (!drawFont) continue;

	if (lastFont && drawFont != lastFont && nspec > 0) {
	    LOCK;
	    XftDrawGlyphFontSpec(fontPtr->ftDraw, xftcolor, specs, nspec);
	    UNLOCK;
	    nspec = 0;
	}
	lastFont = drawFont;

	specs[nspec].font  = drawFont;
	specs[nspec].glyph = shaped->glyphs[i].glyphId;

	if (angle_deg == 0.0) {
	    specs[nspec].x = (int)(originX + shaped->glyphs[i].x + 0.5);
	    specs[nspec].y = (int)(originY + shaped->glyphs[i].y + 0.5);
	} else {
	    double gx = shaped->glyphs[i].x;
	    double gy = shaped->glyphs[i].y;
	    specs[nspec].x = (int)(originX + gx * cosA - gy * sinA + 0.5);
	    specs[nspec].y = (int)(originY + gx * sinA + gy * cosA + 0.5);
	}

	if (++nspec >= NUM_SPEC) {
	    LOCK;
	    XftDrawGlyphFontSpec(fontPtr->ftDraw, xftcolor, specs, nspec);
	    UNLOCK;
	    nspec = 0;
	    lastFont = NULL;
	}

	penX += shaped->glyphs[i].advanceX;
    }

    if (nspec > 0) {
	LOCK;
	XftDrawGlyphFontSpec(fontPtr->ftDraw, xftcolor, specs, nspec);
	UNLOCK;
    }

    if (tsdPtr->clipRegion) {
	XftDrawSetClip(fontPtr->ftDraw, NULL);
    }

    /* Underline / overstrike (rotated if necessary). */
    if (fontPtr->font.fa.underline || fontPtr->font.fa.overstrike) {
	double totalAdvX = (double)penX * cosA;
	double totalAdvY = (double)penX * sinA;

	XPoint points[5];
	double barH = (double)fontPtr->font.underlineHeight;
	double dy;

	if (fontPtr->font.fa.underline) {
	    dy = (double)fontPtr->font.underlinePos;
	    if (barH <= 1.0) dy += 1.0;

	    points[0].x = (int)(originX + dy * sinA + 0.5);
	    points[0].y = (int)(originY + dy * cosA + 0.5);
	    points[1].x = (int)(points[0].x + totalAdvX + 0.5);
	    points[1].y = (int)(points[0].y + totalAdvY + 0.5);

	    if (barH <= 1.0) {
		XDrawLines(display, drawable, gc, points, 2, CoordModeOrigin);
	    } else {
		points[2].x = points[1].x + (int)(barH * sinA + 0.5);
		points[2].y = points[1].y + (int)(-barH * cosA + 0.5);
		points[3].x = points[0].x + (int)(barH * sinA + 0.5);
		points[3].y = points[0].y + (int)(-barH * cosA + 0.5);
		points[4] = points[0];
		XFillPolygon(display, drawable, gc, points, 5, Complex, CoordModeOrigin);
		XDrawLines(display, drawable, gc, points, 5, CoordModeOrigin);
	    }
	}

	if (fontPtr->font.fa.overstrike) {
	    dy = - (double)fontPtr->font.fm.descent - (fontPtr->font.fm.ascent / 10.0);

	    points[0].x = (int)(originX + dy * sinA + 0.5);
	    points[0].y = (int)(originY + dy * cosA + 0.5);
	    points[1].x = (int)(points[0].x + totalAdvX + 0.5);
	    points[1].y = (int)(points[0].y + totalAdvY + 0.5);

	    if (barH <= 1.0) {
		XDrawLines(display, drawable, gc, points, 2, CoordModeOrigin);
	    } else {
		points[2].x = points[1].x + (int)(barH * sinA + 0.5);
		points[2].y = points[1].y + (int)(-barH * cosA + 0.5);
		points[3].x = points[0].x + (int)(barH * sinA + 0.5);
		points[3].y = points[0].y + (int)(-barH * cosA + 0.5);
		points[4] = points[0];
		XFillPolygon(display, drawable, gc, points, 5, Complex, CoordModeOrigin);
		XDrawLines(display, drawable, gc, points, 5, CoordModeOrigin);
	    }
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * GetFontForFace --
 *
 *	Get rotated XftFont for a specific face index.
 *
 * Results:
 *	Returns an XftFont pointer.
 *
 * Side effects:
 *	May create a new rotated font variant.
 *
 *----------------------------------------------------------------------
 */

static XftFont *
GetFontForFace(UnixFtFont *fontPtr, int faceIndex, double angle)
{
    if (faceIndex < 0 || faceIndex >= fontPtr->nfaces) {
	faceIndex = 0;
    }
    if (angle == 0.0) {
	return fontPtr->faces[faceIndex].ft0Font ?
	       fontPtr->faces[faceIndex].ft0Font :
	       fontPtr->faces[0].ft0Font;
    }

    if (!fontPtr->faces[faceIndex].ftFont ||
	fontPtr->faces[faceIndex].angle != angle) {

	FcPattern *pat = FcFontRenderPrepare(NULL, fontPtr->pattern,
					     fontPtr->faces[faceIndex].source);
	double s = sin(angle * M_PI / 180.0);
	double c = cos(angle * M_PI / 180.0);
	FcMatrix mat = {c, -s, s, c};

	FcPatternAddMatrix(pat, FC_MATRIX, &mat);

	LOCK;
	XftFont *ftFont = XftFontOpenPattern(fontPtr->display, pat);
	UNLOCK;

	if (ftFont) {
	    if (fontPtr->faces[faceIndex].ftFont) {
		LOCK;
		XftFontClose(fontPtr->display, fontPtr->faces[faceIndex].ftFont);
		UNLOCK;
	    }
	    fontPtr->faces[faceIndex].ftFont = ftFont;
	    fontPtr->faces[faceIndex].angle = angle;
	}
    }

    return fontPtr->faces[faceIndex].ftFont ?
	   fontPtr->faces[faceIndex].ftFont :
	   fontPtr->faces[0].ft0Font;
}

/*
 *----------------------------------------------------------------------
 *
 * InitFontErrorProc --
 *
 *	Error handler for font initialization.
 *
 * Results:
 *	Always returns 0.
 *
 * Side effects:
 *	Sets the error flag in client data.
 *
 *----------------------------------------------------------------------
 */

static int
InitFontErrorProc(void *clientData,
		  TCL_UNUSED(XErrorEvent *))
{
    int *errorFlagPtr = (int *)clientData;
    if (errorFlagPtr) *errorFlagPtr = 1;
    return 0;
}

/*
 * Local Variables:
 * c-basic-offset: 4
 * fill-column: 78
 * End:
 */
