/*
 * tkMacOSXFont.c --
 *
 *	Contains the Macintosh implementation of the platform-independent font
 *	package interface.
 *
 * Copyright © 2002-2004 Benjamin Riefenstahl, Benjamin.Riefenstahl@epost.de
 * Copyright © 2006-2009 Daniel A. Steffen <das@users.sourceforge.net>
 * Copyright © 2008-2009 Apple Inc.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "tkMacOSXPrivate.h"
#include "tkMacOSXFont.h"
#include "tkMacOSXConstants.h"

#define fixedPitch kCTFontUserFixedPitchFontType

/*
#ifdef TK_MAC_DEBUG
#define TK_MAC_DEBUG_FONTS
#endif
*/

/*
 * The following structure represents our Macintosh-specific implementation
 * of a font object.
 */

typedef struct {
    TkFont font;		/* Stuff used by generic font package. Must be
				 * first in structure. */
    NSFont *nsFont;
    NSDictionary *nsAttributes;
} MacFont;

/*
 * The names for our "native" fonts.
 */

#define SYSTEMFONT_NAME		"system"
#define APPLFONT_NAME		"application"
#define MENUITEMFONT_NAME	"menu"

struct SystemFontMapEntry {
    ThemeFontID id;
    const char *systemName;
    const char *tkName;
    const char *tkName1;
};

#define ThemeFont(n, ...) { kTheme##n##Font, "system" #n "Font", ##__VA_ARGS__ }
static const struct SystemFontMapEntry systemFontMap[] = {
    ThemeFont(System,			"TkDefaultFont", "TkIconFont"),
    ThemeFont(EmphasizedSystem,		"TkCaptionFont", NULL),
    ThemeFont(SmallSystem,		"TkHeadingFont", "TkTooltipFont"),
    ThemeFont(SmallEmphasizedSystem, NULL, NULL),
    ThemeFont(Application,		"TkTextFont", NULL),
    ThemeFont(Label,			"TkSmallCaptionFont", NULL),
    ThemeFont(Views, NULL, NULL),
    ThemeFont(MenuTitle, NULL, NULL),
    ThemeFont(MenuItem,			"TkMenuFont", NULL),
    ThemeFont(MenuItemMark, NULL, NULL),
    ThemeFont(MenuItemCmdKey, NULL, NULL),
    ThemeFont(WindowTitle, NULL, NULL),
    ThemeFont(PushButton, NULL, NULL),
    ThemeFont(UtilityWindowTitle, NULL, NULL),
    ThemeFont(AlertHeader, NULL, NULL),
    ThemeFont(Toolbar, NULL, NULL),
    ThemeFont(MiniSystem, NULL, NULL),
    { kThemeSystemFontDetail,		"systemDetailSystemFont", NULL, NULL },
    { kThemeSystemFontDetailEmphasized,	"systemDetailEmphasizedSystemFont", NULL, NULL },
    { (ThemeFontID)-1, NULL, NULL, NULL }
};
#undef ThemeFont

static int antialiasedTextEnabled = -1;
static NSCharacterSet *whitespaceCharacterSet = nil;
static NSCharacterSet *lineendingCharacterSet = nil;

static void		GetTkFontAttributesForNSFont(NSFont *nsFont,
			    TkFontAttributes *faPtr);
static NSFont *		FindNSFont(const char *familyName,
			    NSFontTraitMask traits, NSInteger weight,
			    CGFloat size, int fallbackToDefault);
static void		InitFont(NSFont *nsFont,
			    const TkFontAttributes *reqFaPtr,
			    MacFont *fontPtr);
static int		CreateNamedSystemFont(Tcl_Interp *interp,
			    Tk_Window tkwin, const char *name,
			    TkFontAttributes *faPtr);

#pragma mark -
#pragma mark Font Helpers:

/*
 * To avoid an extra copy, a TKNSString object wraps a Tcl_DString with an
 * NSString that uses the DString's buffer as its character buffer.  It can be
 * constructed from a Tcl_DString and it has a DString property that handles
 * converting from an NSString to a Tcl_DString.
 */

@implementation TKNSString

- (instancetype)initWithTclUtfBytes:(const void *)bytes
		       length:(NSUInteger)len
{
    self = [self init];
    if (self) {
	Tcl_DStringInit(&_ds);
	Tcl_UtfToChar16DString((const char *)bytes, len, &_ds);
	_string = [[NSString alloc]
	     initWithCharactersNoCopy:(unichar *)Tcl_DStringValue(&_ds)
			       length:Tcl_DStringLength(&_ds)>>1
			 freeWhenDone:NO];
	self.UTF8String = _string.UTF8String;
    }
    return self;
}

- (instancetype)initWithString:(NSString *)aString
{
    self = [self init];
    if (self) {
	_string = [[NSString alloc] initWithString:aString];
	_UTF8String = _string.UTF8String;
    }
    return self;
}

- (void)dealloc
{
    Tcl_DStringFree(&_ds);
    [_string release];
    [super dealloc];
}

- (NSUInteger)length
{
    return _string.length;
}

- (unichar)characterAtIndex:(NSUInteger)index
{
    return [_string characterAtIndex:index];
}

- (Tcl_DString)DString
{
    if ( _ds.string == NULL) {

	/*
	 * The DString has not been initialized. Construct it from
	 * our string's unicode characters.
	 */
	char *p;
	NSUInteger index;

	Tcl_DStringInit(&_ds);
	Tcl_DStringSetLength(&_ds, 3 * [_string length]);
	p = Tcl_DStringValue(&_ds);
	for (index = 0; index < [_string length]; index++) {
	    p += Tcl_UniCharToUtf([_string characterAtIndex: index]|TCL_COMBINE, p);
	}
	Tcl_DStringSetLength(&_ds, (Tcl_Size)(p - Tcl_DStringValue(&_ds)));
    }
    return _ds;
}

@synthesize UTF8String = _UTF8String;
@synthesize DString = _ds;
@end

#define GetNSFontTraitsFromTkFontAttributes(faPtr) \
	((faPtr)->weight == TK_FW_BOLD ? NSBoldFontMask : NSUnboldFontMask) | \
	((faPtr)->slant == TK_FS_ITALIC ? NSItalicFontMask : NSUnitalicFontMask)

/*
 *---------------------------------------------------------------------------
 *
 * GetTkFontAttributesForNSFont --
 *
 *	Fill in TkFontAttributes for given NSFont.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

static void
GetTkFontAttributesForNSFont(
    NSFont *nsFont,
    TkFontAttributes *faPtr)
{
    NSFontTraitMask traits = [[NSFontManager sharedFontManager]
	    traitsOfFont:nsFont];
    faPtr->family = Tk_GetUid([[nsFont familyName] UTF8String]);
#define FACTOR 0.75
    faPtr->size = [nsFont pointSize] * FACTOR;
    faPtr->weight = (traits & NSBoldFontMask ? TK_FW_BOLD : TK_FW_NORMAL);
    faPtr->slant = (traits & NSItalicFontMask ? TK_FS_ITALIC : TK_FS_ROMAN);

}

/*
 *---------------------------------------------------------------------------
 *
 * FindNSFont --
 *
 *	Find NSFont for given attributes. Use default values for missing
 *	attributes, and do a case-insensitive search for font family names
 *	if necessary. If fallbackToDefault flag is set, use the system font
 *	as a last resort.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

static NSFont *
FindNSFont(
    const char *familyName,
    NSFontTraitMask traits,
    NSInteger weight,
    CGFloat size,
    int fallbackToDefault)
{
    NSFontManager *fm = [NSFontManager sharedFontManager];
    NSFont *nsFont, *dflt = nil;
    #define defaultFont (dflt ? dflt : (dflt = [NSFont systemFontOfSize:0]))
    NSString *family;

    if (familyName) {
	family = [[[TKNSString alloc] initWithTclUtfBytes:familyName length:TCL_INDEX_NONE] autorelease];
    } else {
	family = [defaultFont familyName];
    }
    if (size == 0.0) {
	size = [defaultFont pointSize] * FACTOR;
    }
    nsFont = [fm fontWithFamily:family traits:traits weight:weight size:size];

    /*
     * A second bug in NSFontManager that Apple created for the Catalina OS
     * causes requests as above to sometimes return fonts with additional
     * traits that were not requested, even though fonts without those unwanted
     * traits exist on the system.  See bug [90d555e088].  As a workaround
     * we ask the font manager to remove any unrequested traits.
     */

    if (nsFont) {
	nsFont = [fm convertFont:nsFont toNotHaveTrait:~traits];
    }
    if (!nsFont) {
	NSArray *availableFamilies = [fm availableFontFamilies];
	NSString *caseFamily = nil;

	for (NSString *f in availableFamilies) {
	    if ([family caseInsensitiveCompare:f] == NSOrderedSame) {
		caseFamily = f;
		break;
	    }
	}
	if (caseFamily) {
	    nsFont = [fm fontWithFamily:caseFamily traits:traits weight:weight
		    size:size];
	}
    }
    if (!nsFont) {
	nsFont = [NSFont fontWithName:family size:size];
    }
    if (!nsFont && fallbackToDefault) {
	nsFont = [fm convertFont:defaultFont toFamily:family];
	nsFont = [fm convertFont:nsFont toSize:size];
	nsFont = [fm convertFont:nsFont toHaveTrait:traits];
    }
    [nsFont retain];
    #undef defaultFont
    return nsFont;
}

/*
 *---------------------------------------------------------------------------
 *
 * InitFont --
 *
 *	Helper for TkpGetNativeFont() and TkpGetFontFromAttributes().
 *
 * Results:
 *	Fills the MacFont structure.
 *
 * Side effects:
 *	Memory allocated.
 *
 *---------------------------------------------------------------------------
 */

static void
InitFont(
    NSFont *nsFont,
    const TkFontAttributes *reqFaPtr,	/* Can be NULL */
    MacFont *fontPtr)
{
    TkFontAttributes *faPtr;
    TkFontMetrics *fmPtr;
    NSDictionary *nsAttributes;
    NSRect bounds;
    CGFloat kern = 0.0;
    NSFontRenderingMode renderingMode = NSFontDefaultRenderingMode;
    int ascent, descent/*, dontAA*/;
    static const UniChar ch[] = {'.', 'W', ' ', 0xc4, 0xc1, 0xc2, 0xc3, 0xc7};
			/* ., W, Space, Auml, Aacute, Acirc, Atilde, Ccedilla */
#define nCh	(sizeof(ch) / sizeof(UniChar))
    CGGlyph glyphs[nCh];
    CGRect boundingRects[nCh];

    fontPtr->font.fid = (Font) fontPtr;
    faPtr = &fontPtr->font.fa;
    if (reqFaPtr) {
	*faPtr = *reqFaPtr;
    } else {
	TkInitFontAttributes(faPtr);
    }
    fontPtr->nsFont = nsFont;

    /*
     * Some don't like antialiasing on fixed-width even if bigger than limit
     */

    // dontAA = [nsFont isFixedPitch] && fontPtr->font.fa.size <= 10;
    if (antialiasedTextEnabled >= 0/* || dontAA*/) {
	renderingMode = (antialiasedTextEnabled == 0/* || dontAA*/) ?
		NSFontIntegerAdvancementsRenderingMode :
		NSFontAntialiasedRenderingMode;
    }
    nsFont = [nsFont screenFontWithRenderingMode:renderingMode];
    GetTkFontAttributesForNSFont(nsFont, faPtr);
    fmPtr = &fontPtr->font.fm;
    fmPtr->ascent = (int)floor([nsFont ascender] + [nsFont leading] + 0.5);
    fmPtr->descent = (int)floor(-[nsFont descender] + 0.5);
    fmPtr->maxWidth = (int)[nsFont maximumAdvancement].width;
    fmPtr->fixed = [nsFont isFixedPitch];   /* Does not work for all fonts */

    /*
     * The ascent, descent and fixed fields are not correct for all fonts, as
     * a workaround deduce that info from the metrics of some typical glyphs,
     * along with screenfont kerning (space advance difference to printer font)
     */

    bounds = [nsFont boundingRectForFont];
    if (CTFontGetGlyphsForCharacters((CTFontRef) nsFont, ch, glyphs, nCh)) {
	fmPtr->fixed = [nsFont advancementForGlyph:glyphs[0]].width ==
		[nsFont advancementForGlyph:glyphs[1]].width;
	bounds = NSRectFromCGRect(CTFontGetBoundingRectsForGlyphs((CTFontRef)
		nsFont, kCTFontOrientationDefault, ch, boundingRects, nCh));
	kern = [nsFont advancementForGlyph:glyphs[2]].width -
		[fontPtr->nsFont advancementForGlyph:glyphs[2]].width;
    }
    descent = (int)floor(-bounds.origin.y + 0.5);
    ascent = (int)floor(bounds.size.height + bounds.origin.y + 0.5);
    if (ascent > fmPtr->ascent) {
	fmPtr->ascent = ascent;
    }
    if (descent > fmPtr->descent) {
	fmPtr->descent = descent;
    }
    nsAttributes = [NSDictionary dictionaryWithObjectsAndKeys:
	    nsFont, NSFontAttributeName,
	    [NSNumber numberWithInt:faPtr->underline ?
		NSUnderlineStyleSingle|NSUnderlinePatternSolid :
		NSUnderlineStyleNone], NSUnderlineStyleAttributeName,
	    [NSNumber numberWithInt:faPtr->overstrike ?
		NSUnderlineStyleSingle|NSUnderlinePatternSolid :
		NSUnderlineStyleNone], NSStrikethroughStyleAttributeName,
	    [NSNumber numberWithInt:fmPtr->fixed ? 0 : 1],
		NSLigatureAttributeName,
	    [NSNumber numberWithDouble:kern], NSKernAttributeName, nil];
    fontPtr->nsAttributes = [nsAttributes retain];
#undef nCh
}

/*
 *-------------------------------------------------------------------------
 *
 * CreateNamedSystemFont --
 *
 *	Register a system font with the Tk named font mechanism.
 *
 * Results:
 *
 *	Result from TkCreateNamedFont().
 *
 * Side effects:
 *
 *	A new named font is added to the Tk font registry.
 *
 *-------------------------------------------------------------------------
 */

static int
CreateNamedSystemFont(
    Tcl_Interp *interp,
    Tk_Window tkwin,
    const char* name,
    TkFontAttributes *faPtr)
{
    TkDeleteNamedFont(NULL, tkwin, name);
    return TkCreateNamedFont(interp, tkwin, name, faPtr);
}

#pragma mark -

#pragma mark Grapheme Cluster indexing

/*
 *----------------------------------------------------------------------
 *
 * startOfClusterObjCmd --
 *
 *      This function is invoked to process the startOfCluster command.
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

static int
startOfClusterObjCmd(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,         /* Current interpreter. */
    Tcl_Size objc,              /* Number of arguments. */
    Tcl_Obj *const objv[])      /* Argument objects. */
{
    TKNSString *S;
    const char *stringArg;
    Tcl_Size len, idx;
    if ((unsigned)(objc - 3) > 1) {
	Tcl_WrongNumArgs(interp, 1 , objv, "str start ?locale?");
	return TCL_ERROR;
    }
    stringArg = Tcl_GetStringFromObj(objv[1], &len);
    if (stringArg == NULL) {
	return TCL_ERROR;
    }
    Tcl_Size ulen = Tcl_GetCharLength(objv[1]);
    S = [[TKNSString alloc] initWithTclUtfBytes:stringArg length:len];
    len = [S length];
    if (TkGetIntForIndex(objv[2], ulen - 1, 0, &idx) != TCL_OK) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"bad index \"%s\": must be integer?[+-]integer?, end?[+-]integer?, or \"\"",
		Tcl_GetString(objv[2])));
	Tcl_SetErrorCode(interp, "TK", "VALUE", "INDEX", (char *)NULL);
	return TCL_ERROR;
    }
    if (idx >= ulen) {
	idx = len;
    } else if (idx > 0 && len != ulen) {
	/* The string contains codepoints > \uFFFF. Determine UTF-16 index */
	Tcl_Size newIdx = 0;
	for (Tcl_Size i = 0; i < idx; i++) {
	    newIdx += 1 + (((newIdx < len-1) && ([S characterAtIndex:newIdx]&0xFC00) == 0xD800) && (([S characterAtIndex:newIdx+1]&0xFC00) == 0xDC00));
	}
	idx = newIdx;
    }
    if (idx >= 0) {
	if (idx >= len) {
	    idx = len;
	} else {
	    NSRange range = [S rangeOfComposedCharacterSequenceAtIndex:idx];
	    idx = range.location;
	}
	if (idx > 0 && len != ulen) {
	    /* The string contains codepoints > \uFFFF. Determine UTF-32 index */
	    Tcl_Size newIdx = 1;
	    for (Tcl_Size i = 1; i < idx; i++) {
		if ((([S characterAtIndex:i-1]&0xFC00) != 0xD800) || (([S characterAtIndex:i]&0xFC00) != 0xDC00)) newIdx++;
	    }
	    idx = newIdx;
	}
	Tcl_SetObjResult(interp, TkNewIndexObj(idx));
    }
    return TCL_OK;
}

static int
endOfClusterObjCmd(
    TCL_UNUSED(void *),
    Tcl_Interp *interp,         /* Current interpreter. */
    Tcl_Size objc,              /* Number of arguments. */
    Tcl_Obj *const objv[])      /* Argument objects. */
{
    TKNSString *S;
    char *stringArg;
    Tcl_Size idx, len;

    if ((unsigned)(objc - 3) > 1) {
	Tcl_WrongNumArgs(interp, 1 , objv, "str start ?locale?");
	return TCL_ERROR;
    }
    stringArg = Tcl_GetStringFromObj(objv[1], &len);
    if (stringArg == NULL) {
	return TCL_ERROR;
    }
    Tcl_Size ulen = Tcl_GetCharLength(objv[1]);
    S = [[TKNSString alloc] initWithTclUtfBytes:stringArg length:len];
    len = [S length];
    if (TkGetIntForIndex(objv[2], ulen - 1, 0, &idx) != TCL_OK) {
	Tcl_SetObjResult(interp, Tcl_ObjPrintf(
		"bad index \"%s\": must be integer?[+-]integer?, end?[+-]integer?, or \"\"",
		Tcl_GetString(objv[2])));
	Tcl_SetErrorCode(interp, "TK", "VALUE", "INDEX", (char *)NULL);
	return TCL_ERROR;
    }
    if (idx >= ulen) {
	idx = len;
    } else if (idx > 0 && len != ulen) {
	/* The string contains codepoints > \uFFFF. Determine UTF-16 index */
	Tcl_Size newIdx = 0;
	for (Tcl_Size i = 0; i < idx; i++) {
	    newIdx += 1 + (((newIdx < len-1) && ([S characterAtIndex:newIdx]&0xFC00) == 0xD800) && (([S characterAtIndex:newIdx+1]&0xFC00) == 0xDC00));
	}
	idx = newIdx;
    }
    if (idx + 1 <= len) {
	if (idx < 0) {
	    idx = 0;
	} else {
	    NSRange range = [S rangeOfComposedCharacterSequenceAtIndex:idx];
	    idx = range.location + range.length;
	    if (idx > 0 && len != ulen) {
		/* The string contains codepoints > \uFFFF. Determine UTF-32 index */
		Tcl_Size newIdx = 1;
		for (Tcl_Size i = 1; i < idx; i++) {
		if ((([S characterAtIndex:i-1]&0xFC00) != 0xD800) || (([S characterAtIndex:i]&0xFC00) != 0xDC00)) newIdx++;
		}
		idx = newIdx;
	    }
	}
	Tcl_SetObjResult(interp, TkNewIndexObj(idx));
    }
    return TCL_OK;
}

#pragma mark -
#pragma mark Font handling:

/*
 *-------------------------------------------------------------------------
 *
 * TkpFontPkgInit --
 *
 *	This procedure is called when an application is created. It
 *	initializes all the structures that are used by the
 *	platform-dependent code on a per application basis.
 *	Note that this is called before TkpInit() !
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Initialize named system fonts.
 *
 *-------------------------------------------------------------------------
 */

void
TkpFontPkgInit(
    TkMainInfo *mainPtr)	/* The application being created. */
{
    Tcl_Interp *interp = mainPtr->interp;
    Tk_Window tkwin = (Tk_Window)mainPtr->winPtr;
    const struct SystemFontMapEntry *systemFont = systemFontMap;
    NSFont *nsFont;
    TkFontAttributes fa;
    NSMutableCharacterSet *cs;
    /*
     * Since we called before TkpInit, we need our own autorelease pool.
     */
    NSAutoreleasePool *pool = [NSAutoreleasePool new];

    /*
     * Force this for now.
     */
    if (!mainPtr->winPtr->mainPtr) {
	mainPtr->winPtr->mainPtr = mainPtr;
    }
    while (systemFont->systemName) {
	nsFont = (NSFont*) CTFontCreateUIFontForLanguage(
		HIThemeGetUIFontType(systemFont->id), 0, NULL);
	if (nsFont) {
	    TkInitFontAttributes(&fa);
	    GetTkFontAttributesForNSFont(nsFont, &fa);
	    CreateNamedSystemFont(interp, tkwin, systemFont->systemName, &fa);
	    if (systemFont->tkName) {
		CreateNamedSystemFont(interp, tkwin, systemFont->tkName, &fa);
	    }
	    if (systemFont->tkName1) {
		CreateNamedSystemFont(interp, tkwin, systemFont->tkName1, &fa);
	    }
	    CFRelease(nsFont);
	}
	systemFont++;
    }
    TkInitFontAttributes(&fa);
#if 0

    /*
     * In macOS 10.15.1 Apple introduced a bug in NSFontManager which caused
     * it to not recognize the familyName ".SF NSMono" which is the familyName
     * of the default fixed pitch system fault on that system.  See bug [855049e799].
     * As a workaround we call [NSFont userFixedPitchFontOfSize:11] instead.
     * This returns a user font in the "Menlo" family.
     */

    nsFont = (NSFont*) CTFontCreateUIFontForLanguage(fixedPitch, 11, NULL);
#else
    nsFont = [NSFont userFixedPitchFontOfSize:11];
#endif
    if (nsFont) {
	GetTkFontAttributesForNSFont(nsFont, &fa);
#if 0
	CFRelease(nsFont);
#endif
    } else {
	fa.family = Tk_GetUid("Monaco");
	fa.size = 11;
	fa.weight = TK_FW_NORMAL;
	fa.slant = TK_FS_ROMAN;
    }
    CreateNamedSystemFont(interp, tkwin, "TkFixedFont", &fa);
    if (!whitespaceCharacterSet) {
	whitespaceCharacterSet = [[NSCharacterSet
		whitespaceAndNewlineCharacterSet] retain];
	cs = [whitespaceCharacterSet mutableCopy];
	[cs removeCharactersInString:@" "];
	lineendingCharacterSet = [cs copy];
	[cs release];
    }
    [pool drain];
    Tcl_CreateObjCommand2(interp, "::tk::startOfCluster", startOfClusterObjCmd, NULL, NULL);
    Tcl_CreateObjCommand2(interp, "::tk::endOfCluster", endOfClusterObjCmd, NULL, NULL);
}

/*
 *---------------------------------------------------------------------------
 *
 * TkpGetNativeFont --
 *
 *	Map a platform-specific native font name to a TkFont.
 *
 * Results:
 *	The return value is a pointer to a TkFont that represents the native
 *	font. If a native font by the given name could not be found, the return
 *	value is NULL.
 *
 *	Every call to this procedure returns a new TkFont structure, even if
 *	the name has already been seen before. The caller should call
 *	TkpDeleteFont() when the font is no longer needed.
 *
 *	The caller is responsible for initializing the memory associated with
 *	the generic TkFont when this function returns and releasing the
 *	contents of the generics TkFont before calling TkpDeleteFont().
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

TkFont *
TkpGetNativeFont(
    TCL_UNUSED(Tk_Window),		/* For display where font will be used. */
    const char *name)		/* Platform-specific font name. */
{
    MacFont *fontPtr = NULL;
    ThemeFontID themeFontId;
    CTFontRef ctFont;

    if (strcmp(name, SYSTEMFONT_NAME) == 0) {
	themeFontId = kThemeSystemFont;
    } else if (strcmp(name, APPLFONT_NAME) == 0) {
	themeFontId = kThemeApplicationFont;
    } else if (strcmp(name, MENUITEMFONT_NAME) == 0) {
	themeFontId = kThemeMenuItemFont;
    } else {
	return NULL;
    }
    ctFont = CTFontCreateUIFontForLanguage(
	    HIThemeGetUIFontType(themeFontId), 0, NULL);
    if (ctFont) {
	fontPtr = (MacFont *)ckalloc(sizeof(MacFont));
	InitFont((NSFont*) ctFont, NULL, fontPtr);
    }

    return (TkFont *) fontPtr;
}

/*
 *---------------------------------------------------------------------------
 *
 * TkpGetFontFromAttributes --
 *
 *	Given a desired set of attributes for a font, find a font with the
 *	closest matching attributes.
 *
 * Results:
 *	The return value is a pointer to a TkFont that represents the font with
 *	the desired attributes. If a font with the desired attributes could not
 *	be constructed, some other font will be substituted automatically.
 *
 *	Every call to this procedure returns a new TkFont structure, even if
 *	the specified attributes have already been seen before. The caller
 *	should call TkpDeleteFont() to free the platform- specific data when
 *	the font is no longer needed.
 *
 *	The caller is responsible for initializing the memory associated with
 *	the generic TkFont when this function returns and releasing the
 *	contents of the generic TkFont before calling TkpDeleteFont().
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

TkFont *
TkpGetFontFromAttributes(
    TkFont *tkFontPtr,		/* If non-NULL, store the information in this
				 * existing TkFont structure, rather than
				 * allocating a new structure to hold the font;
				 * the existing contents of the font will be
				 * released. If NULL, a new TkFont structure is
				 * allocated. */
    Tk_Window tkwin,		/* For display where font will be used. */
    const TkFontAttributes *faPtr)
				/* Set of attributes to match. */
{
    MacFont *fontPtr;
    CGFloat points = floor(TkFontGetPoints(tkwin, faPtr->size / FACTOR) + 0.5);
    NSFontTraitMask traits = GetNSFontTraitsFromTkFontAttributes(faPtr);
    NSInteger weight = (faPtr->weight == TK_FW_BOLD ? 9 : 5);
    NSFont *nsFont;

    nsFont = FindNSFont(faPtr->family, traits, weight, points, 0);
    if (!nsFont) {
	const char *const *aliases = TkFontGetAliasList(faPtr->family);

	while (aliases && !nsFont) {
	    nsFont = FindNSFont(*aliases++, traits, weight, points, 0);
	}
    }
    if (!nsFont) {
	nsFont = FindNSFont(faPtr->family, traits, weight, points, 1);
    }
    if (!nsFont) {
	Tcl_Panic("Could not determine NSFont from TkFontAttributes");
    }
    if (tkFontPtr == NULL) {
	fontPtr = (MacFont *)ckalloc(sizeof(MacFont));
    } else {
	fontPtr = (MacFont *)tkFontPtr;
	TkpDeleteFont(tkFontPtr);
    }
    CFRetain(nsFont); /* Always needed to allow unconditional CFRelease below */
    InitFont(nsFont, faPtr, fontPtr);

    return (TkFont *) fontPtr;
}

/*
 *---------------------------------------------------------------------------
 *
 * TkpDeleteFont --
 *
 *	Called to release a font allocated by TkpGetNativeFont() or
 *	TkpGetFontFromAttributes(). The caller should have already released the
 *	fields of the TkFont that are used exclusively by the generic TkFont
 *	code.
 *
 * Results:
 *	TkFont is deallocated.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

void
TkpDeleteFont(
    TkFont *tkFontPtr)		/* Token of font to be deleted. */
{
    MacFont *fontPtr = (MacFont *) tkFontPtr;

    [fontPtr->nsAttributes release];
    fontPtr->nsAttributes = NULL;
    CFRelease(fontPtr->nsFont); /* Either a CTFontRef or a CFRetained NSFont */
}

/*
 *---------------------------------------------------------------------------
 *
 * TkpGetFontFamilies --
 *
 *	Return information about the font families that are available on the
 *	display of the given window.
 *
 * Results:
 *	Modifies interp's result object to hold a list of all the available
 *	font families.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

void
TkpGetFontFamilies(
    Tcl_Interp *interp,		/* Interp to hold result. */
    TCL_UNUSED(Tk_Window))		/* For display to query. */
{
    Tcl_Obj *resultPtr = Tcl_NewListObj(0, NULL);
    NSArray *list = [[NSFontManager sharedFontManager] availableFontFamilies];

    for (NSString *family in list) {
	Tcl_ListObjAppendElement(NULL, resultPtr,
		Tcl_NewStringObj([family UTF8String], TCL_INDEX_NONE));
    }
    Tcl_SetObjResult(interp, resultPtr);
}

/*
 *-------------------------------------------------------------------------
 *
 * TkpGetSubFonts --
 *
 *	A function used by the testing package for querying the actual screen
 *	fonts that make up a font object.
 *
 * Results:
 *	Modifies interp's result object to hold a list containing the names of
 *	the screen fonts that make up the given font object.
 *
 * Side effects:
 *	None.
 *
 *-------------------------------------------------------------------------
 */

void
TkpGetSubFonts(
    Tcl_Interp *interp,		/* Interp to hold result. */
    Tk_Font tkfont)		/* Font object to query. */
{
    MacFont *fontPtr = (MacFont *) tkfont;
    Tcl_Obj *resultPtr = Tcl_NewListObj(0, NULL);

    if (fontPtr->nsFont) {
	NSArray *list = [[fontPtr->nsFont fontDescriptor]
		objectForKey:NSFontCascadeListAttribute];

	for (NSFontDescriptor *subFontDesc in list) {
	    NSString *family = [subFontDesc objectForKey:NSFontFamilyAttribute];

	    if (family) {
		Tcl_ListObjAppendElement(NULL, resultPtr,
			Tcl_NewStringObj([family UTF8String], TCL_INDEX_NONE));
	    }
	}
    }
    Tcl_SetObjResult(interp, resultPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * TkpGetFontAttrsForChar --
 *
 *	Retrieve the font attributes of the actual font used to render a given
 *	character.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The font attributes are stored in *faPtr.
 *
 *----------------------------------------------------------------------
 */

void
TkpGetFontAttrsForChar(
    TCL_UNUSED(Tk_Window),		/* Window on the font's display */
    Tk_Font tkfont,		/* Font to query */
    int c,			/* Character of interest */
    TkFontAttributes* faPtr)	/* Output: Font attributes */
{
    MacFont *fontPtr = (MacFont *) tkfont;
    NSFont *nsFont = fontPtr->nsFont;
    *faPtr = fontPtr->font.fa;
    if (nsFont && ![[nsFont coveredCharacterSet] characterIsMember:c]) {
	UTF16Char ch = (UTF16Char) c;

	nsFont = [nsFont bestMatchingFontForCharacters:&ch
		length:1 attributes:nil actualCoveredLength:NULL];
	if (nsFont) {
	    GetTkFontAttributesForNSFont(nsFont, faPtr);
	}
    }
}

#pragma mark -
#pragma mark Measuring and drawing:

/*
 *---------------------------------------------------------------------------
 *
 * Tk_MeasureChars --
 *
 *	Determine the number of characters from the string that will fit in
 *	the given horizontal span. The measurement is done under the
 *	assumption that Tk_DrawChars() will be used to actually display the
 *	characters.
 *
 *	With ATSUI we need the line context to do this right, so we have the
 *	actual implementation in Tk_MeasureCharsInContext().
 *
 * Results:
 *	The return value is the number of bytes from source that fit into the
 *	span that extends from 0 to maxLength. *lengthPtr is filled with the
 *	x-coordinate of the right edge of the last character that did fit.
 *
 * Side effects:
 *	None.
 *
 * Todo:
 *	Effects of the "flags" parameter are untested.
 *
 *---------------------------------------------------------------------------
 */

int
Tk_MeasureChars(
    Tk_Font tkfont,		/* Font in which characters will be drawn. */
    const char *source,		/* UTF-8 string to be displayed. Need not be
				 * '\0' terminated. */
    Tcl_Size numBytes,		/* Maximum number of bytes to consider from
				 * source string. */
    int maxLength,		/* If >= 0, maxLength specifies the longest
				 * permissible line length; don't consider any
				 * character that would cross this x-position.
				 * If < 0, then line length is unbounded and
				 * the flags argument is ignored. */
    int flags,			/* Various flag bits OR-ed together:
				 * TK_PARTIAL_OK means include the last char
				 * which only partially fit on this line.
				 * TK_WHOLE_WORDS means stop on a word
				 * boundary, if possible. TK_AT_LEAST_ONE
				 * means return at least one character even if
				 * no characters fit. */
    int *lengthPtr)		/* Filled with x-location just after the
				 * terminating character. */
{
    return Tk_MeasureCharsInContext(tkfont, source, numBytes, 0, numBytes,
	    maxLength, flags, lengthPtr);
}

/*
 *---------------------------------------------------------------------------
 *
 * Tk_MeasureCharsInContext --
 *
 *	Determine the number of bytes from the string that will fit in the
 *	given horizontal span. The measurement is done under the assumption
 *	that Tk_DrawCharsInContext() will be used to actually display the
 *	characters.
 *
 *	This one is almost the same as Tk_MeasureChars(), but with access to
 *	all the characters on the line for context.
 *
 * Results:
 *	The return value is the number of bytes from source that fit into the
 *	span that extends from 0 to maxLength. *lengthPtr is filled with the
 *	x-coordinate of the right edge of the last character that did fit.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

int
Tk_MeasureCharsInContext(
    Tk_Font tkfont,		/* Font in which characters will be drawn. */
    const char * source,	/* UTF-8 string to be displayed. Need not be
				 * '\0' terminated. */
    Tcl_Size numBytes,		/* Maximum number of bytes to consider from
				 * source string in all. */
    Tcl_Size rangeStart,		/* Index of first byte to measure. */
    Tcl_Size rangeLength,		/* Length of range to measure in bytes. */
    int maxLength,		/* If >= 0, maxLength specifies the longest
				 * permissible line length; don't consider any
				 * character that would cross this x-position.
				 * If < 0, then line length is unbounded and
				 * the flags argument is ignored. */
    int flags,			/* Various flag bits OR-ed together:
				 * TK_PARTIAL_OK means include the last char
				 * which only partially fits on this line.
				 * TK_WHOLE_WORDS means stop on a word
				 * boundary, if possible. TK_AT_LEAST_ONE means
				 * return at least one character even if no
				 * characters fit.  If TK_WHOLE_WORDS and
				 * TK_AT_LEAST_ONE are set and the first word
				 * doesn't fit, we return at least one
				 * character or whatever characters fit into
				 * maxLength.  TK_ISOLATE_END means that the
				 * last character should not be considered in
				 * context with the rest of the string (used
				 * for breaking lines). */
    int *lengthPtr)		/* Filled with x-location just after the
				 * terminating character. */
{
    const MacFont *fontPtr = (const MacFont *) tkfont;
    NSString *string;
    NSAttributedString *attributedString;
    CTTypesetterRef typesetter;
    CFIndex start, len;
    CFRange range = {0, 0};
    CTLineRef line;
    CGFloat offset = 0;
    CFIndex index;
    double width;
    int length, fit;

    if (rangeStart < 0 || rangeLength <= 0 ||
	    rangeStart + rangeLength > numBytes ||
	    (maxLength == 0 && !(flags & TK_AT_LEAST_ONE))) {
	*lengthPtr = 0;
	return 0;
    }
    if (maxLength > 32767) {
	maxLength = 32767;
    }
    string = [[TKNSString alloc] initWithTclUtfBytes:source length:numBytes];
    if (!string) {
	length = 0;
	fit = rangeLength;
	goto done;
    }
    attributedString = [[NSAttributedString alloc] initWithString:string
	    attributes:fontPtr->nsAttributes];
    typesetter = CTTypesetterCreateWithAttributedString(
	    (CFAttributedStringRef)attributedString);
    start = TclNumUtfChars(source, rangeStart);
    len = TclNumUtfChars(source + rangeStart, rangeLength);
    if (start > 0) {
	range.length = start;
	line = CTTypesetterCreateLine(typesetter, range);
	offset = CTLineGetTypographicBounds(line, NULL, NULL, NULL);
	CFRelease(line);
    }
    if (maxLength < 0) {
	index = len;
	range.length = len;
	line = CTTypesetterCreateLine(typesetter, range);
	width = CTLineGetTypographicBounds(line, NULL, NULL, NULL);
	CFRelease(line);
    } else {
	double maxWidth = maxLength + offset;
	NSCharacterSet *cs;

	/*
	 * Get a line breakpoint in the source string.
	 */

	index = start;
	if (flags & TK_WHOLE_WORDS) {
	    index = CTTypesetterSuggestLineBreak(typesetter, start, maxWidth);
	    if (index <= start && (flags & TK_AT_LEAST_ONE)) {
		flags &= ~TK_WHOLE_WORDS;
	    }
	}
	if (index <= start && !(flags & TK_WHOLE_WORDS)) {
	    index = CTTypesetterSuggestClusterBreak(typesetter, start, maxWidth);
	}

	/*
	 * Trim right whitespace/lineending characters.
	 */

	cs = (index <= len && (flags & TK_WHOLE_WORDS)) ?
		whitespaceCharacterSet : lineendingCharacterSet;
	while (index > start &&
		[cs characterIsMember:[string characterAtIndex:(index - 1)]]) {
	    index--;
	}

	/*
	 * If there is no line breakpoint in the source string between its
	 * start and the index position that fits in maxWidth, then
	 * CTTypesetterSuggestLineBreak() returns that very last index.
	 * However if the TK_WHOLE_WORDS flag is set, we want to break at a
	 * word boundary. In this situation, unless TK_AT_LEAST_ONE is set, we
	 * must report that zero chars actually fit (in other words the
	 * smallest word of the source string is still larger than maxWidth).
	 */

	if ((index >= start) && (index < len) &&
		(flags & TK_WHOLE_WORDS) && !(flags & TK_AT_LEAST_ONE) &&
		![cs characterIsMember:[string characterAtIndex:index]]) {
	    index = start;
	}

	if (index <= start && (flags & TK_AT_LEAST_ONE)) {
	    index = start + 1;
	}

	/*
	 * Now measure the string width in pixels.
	 */

	if (index > 0) {
	    range.length = index;
	    line = CTTypesetterCreateLine(typesetter, range);
	    width = CTLineGetTypographicBounds(line, NULL, NULL, NULL);
	    CFRelease(line);
	} else {
	    width = 0;
	}
	if (width < maxWidth && (flags & TK_PARTIAL_OK) && index < len) {
	    range.length = ++index;
	    line = CTTypesetterCreateLine(typesetter, range);
	    width = CTLineGetTypographicBounds(line, NULL, NULL, NULL);
	    CFRelease(line);
	}

	/*
	 * The call to CTTypesetterSuggestClusterBreak above will always return
	 * at least one character regardless of whether it exceeded it or not.
	 * Clean that up now.
	 */

	while (width > maxWidth && !(flags & TK_PARTIAL_OK)
		&& index > start+(flags & TK_AT_LEAST_ONE)) {
	    range.length = --index;
	    line = CTTypesetterCreateLine(typesetter, range);
	    width = CTLineGetTypographicBounds(line, NULL, NULL, NULL);
	    CFRelease(line);
	}

    }
    CFRelease(typesetter);
    [attributedString release];
    [string release];
    length = ceil(width - offset);
    fit = (TclUtfAtIndex(source, index) - source) - rangeStart;
done:
#ifdef TK_MAC_DEBUG_FONTS
    TkMacOSXDbgMsg("measure: source=\"%s\" range=\"%.*s\" maxLength=%d "
	    "flags='%s%s%s%s' -> width=%d bytesFit=%d\n", source, rangeLength,
	    source+rangeStart, maxLength,
	    flags & TK_PARTIAL_OK   ? "partialOk "  : "",
	    flags & TK_WHOLE_WORDS  ? "wholeWords " : "",
	    flags & TK_AT_LEAST_ONE ? "atLeastOne " : "",
	    flags & TK_ISOLATE_END  ? "isolateEnd " : "",
	    length, fit);
#endif
    *lengthPtr = length;
    return fit;
}

/*
 *---------------------------------------------------------------------------
 *
 * Tk_DrawChars --
 *
 *	Draw a string of characters on the screen.
 *
 *	With ATSUI we need the line context to do this right, so we have the
 *	actual implementation in TkpDrawAngledCharsInContext().
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Information gets drawn on the screen.
 *
 *---------------------------------------------------------------------------
 */

void
Tk_DrawChars(
    Display *display,		/* Display on which to draw. */
    Drawable drawable,		/* Window or pixmap in which to draw. */
    GC gc,			/* Graphics context for drawing characters. */
    Tk_Font tkfont,		/* Font in which characters will be drawn; must
				 * be the same as font used in GC. */
    const char *source,		/* UTF-8 string to be displayed. Need not be
				 * '\0' terminated. All Tk meta-characters
				 * (tabs, control characters, and newlines)
				 * should be stripped out of the string that is
				 * passed to this function. If they are not
				 * stripped out, they will be displayed as
				 * regular printing characters. */
    Tcl_Size numBytes,		/* Number of bytes in string. */
    int x, int y)		/* Coordinates at which to place origin of the
				 * string when drawing. */
{
    TkpDrawAngledCharsInContext(display, drawable, gc, tkfont, source, numBytes,
	    0, numBytes, x, y, 0.0);
}

void
TkDrawAngledChars(
    Display *display,		/* Display on which to draw. */
    Drawable drawable,		/* Window or pixmap in which to draw. */
    GC gc,			/* Graphics context for drawing characters. */
    Tk_Font tkfont,		/* Font in which characters will be drawn;
				 * must be the same as font used in GC. */
    const char *source,		/* UTF-8 string to be displayed. Need not be
				 * '\0' terminated. All Tk meta-characters
				 * (tabs, control characters, and newlines)
				 * should be stripped out of the string that is
				 * passed to this function. If they are not
				 * stripped out, they will be displayed as
				 * regular printing characters. */
    Tcl_Size numBytes,		/* Number of bytes in string. */
    double x, double y,		/* Coordinates at which to place origin of
				 * string when drawing. */
    double angle)		/* What angle to put text at, in degrees. */
{
    TkpDrawAngledCharsInContext(display, drawable, gc, tkfont, source, numBytes,
	    0, numBytes, x, y, angle);
}

/*
 *---------------------------------------------------------------------------
 *
 * Tk_DrawCharsInContext --
 *
 *	Draw a string of characters on the screen like Tk_DrawChars(), with
 *	access to all the characters on the line for context.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Information gets drawn on the screen.
 *
 * Todo:
 *	Stippled text drawing.
 *
 *---------------------------------------------------------------------------
 */

void
Tk_DrawCharsInContext(
    Display *display,		/* Display on which to draw. */
    Drawable drawable,		/* Window or pixmap in which to draw. */
    GC gc,			/* Graphics context for drawing characters. */
    Tk_Font tkfont,		/* Font in which characters will be drawn; must
				 * be the same as font used in GC. */
    const char * source,	/* UTF-8 string to be displayed. Need not be
				 * '\0' terminated. All Tk meta-characters
				 * (tabs, control characters, and newlines)
				 * should be stripped out of the string that is
				 * passed to this function. If they are not
				 * stripped out, they will be displayed as
				 * regular printing characters. */
    Tcl_Size numBytes,		/* Number of bytes in string. */
    Tcl_Size rangeStart,		/* Index of first byte to draw. */
    Tcl_Size rangeLength,		/* Length of range to draw in bytes. */
    int x, int y)		/* Coordinates at which to place origin of the
				 * whole (not just the range) string when
				 * drawing. */
{
    TkpDrawAngledCharsInContext(display, drawable, gc, tkfont, source, numBytes,
	    rangeStart, rangeLength, x, y, 0.0);
}

void
TkpDrawAngledCharsInContext(
    TCL_UNUSED(Display *),		/* Display on which to draw. */
    Drawable drawable,		/* Window or pixmap in which to draw. */
    GC gc,			/* Graphics context for drawing characters. */
    Tk_Font tkfont,		/* Font in which characters will be drawn; must
				 * be the same as font used in GC. */
    const char * source,	/* UTF-8 string to be displayed. Need not be
				 * '\0' terminated. All Tk meta-characters
				 * (tabs, control characters, and newlines)
				 * should be stripped out of the string that is
				 * passed to this function. If they are not
				 * stripped out, they will be displayed as
				 * regular printing characters. */
    Tcl_Size numBytes,		/* Number of bytes in string. */
    Tcl_Size rangeStart,		/* Index of first byte to draw. */
    Tcl_Size rangeLength,		/* Length of range to draw in bytes. */
    double x, double y,		/* Coordinates at which to place origin of the
				 * whole (not just the range) string when
				 * drawing. */
    double angle)		/* What angle to put text at, in degrees. */
{
    const MacFont *fontPtr = (const MacFont *) tkfont;
    NSString *string;
    NSMutableDictionary *attributes;
    NSAttributedString *attributedString;
    CTTypesetterRef typesetter;
    CFIndex start, length;
    CTLineRef line, full=nil;
    MacDrawable *macWin = (MacDrawable *)drawable;
    TkMacOSXDrawingContext drawingContext;
    CGContextRef context;
    CGColorRef fg = NULL;
    NSFont *nsFont;
    CGAffineTransform t;
    CGFloat width, height, textX = (CGFloat) x, textY = (CGFloat) y;

    if (rangeStart < 0 || rangeLength <= 0
	    || rangeStart + rangeLength > numBytes
	    || !TkMacOSXSetupDrawingContext(drawable, gc, &drawingContext)) {
	return;
    }
    string = [[TKNSString alloc] initWithTclUtfBytes:source length:numBytes];
    if (!string) {
	return;
    }

    context = drawingContext.context;
    TkSetMacColor(gc->foreground, &fg);
    attributes = [fontPtr->nsAttributes mutableCopy];
    if (fg) {
	[attributes setObject:(id)fg forKey:(id)kCTForegroundColorAttributeName];
	CGColorRelease(fg);
    }
    nsFont = [attributes objectForKey:NSFontAttributeName];
    [nsFont setInContext:GET_NSCONTEXT(context, NO)];
    CGContextSetTextMatrix(context, CGAffineTransformIdentity);
    attributedString = [[NSAttributedString alloc] initWithString:string
	    attributes:attributes];
    [string release];
    typesetter = CTTypesetterCreateWithAttributedString(
	    (CFAttributedStringRef)attributedString);
    textX += (CGFloat) macWin->xOff;
    textY += (CGFloat) macWin->yOff;
    height = [drawingContext.view bounds].size.height;
    textY = height - textY;
    t = CGAffineTransformMake(1.0, 0.0, 0.0, -1.0, 0.0, height);
    if (angle != 0.0) {
	t = CGAffineTransformTranslate(
	     CGAffineTransformRotate(
		 CGAffineTransformTranslate(t, textX, textY), angle*PI/180.0),
	     -textX, -textY);
    }
    CGContextConcatCTM(context, t);
    start = TclNumUtfChars(source, rangeStart);
    length = TclNumUtfChars(source, rangeStart + rangeLength) - start;
    line = CTTypesetterCreateLine(typesetter, CFRangeMake(start, length));
    if (start > 0) {

	/*
	 * We are only drawing part of the string.  To compute the x coordinate
	 * of the part we are drawing we subtract its typographical length from
	 * the typographical length of the full string.  This accounts for the
	 * kerning after the initial part of the string.
	 */

	full = CTTypesetterCreateLine(typesetter, CFRangeMake(0, start + length));
	width = CTLineGetTypographicBounds(full, NULL, NULL, NULL);
	CFRelease(full);
	textX += (width - CTLineGetTypographicBounds(line, NULL, NULL, NULL));
    }
    CGContextSetTextPosition(context, textX, textY);
    CTLineDraw(line, context);
    CFRelease(line);
    CFRelease(typesetter);
    [attributedString release];
    [attributes release];
    TkMacOSXRestoreDrawingContext(&drawingContext);
}

#pragma mark -
#pragma mark Accessors:

/*
 *---------------------------------------------------------------------------
 *
 * TkMacOSXNSFontForFont --
 *
 *	Return an NSFont for the given Tk_Font.
 *
 * Results:
 *	NSFont*.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

MODULE_SCOPE NSFont*
TkMacOSXNSFontForFont(
    Tk_Font tkfont)
{
    return tkfont ? ((MacFont *)tkfont)->nsFont : nil;
}

/*
 *---------------------------------------------------------------------------
 *
 * TkMacOSXNSFontAttributesForFont --
 *
 *	Return an NSDictionary of font attributes for the given Tk_Font.
 *
 * Results:
 *	NSFont*.
 *
 * Side effects:
 *	None.
 *
 *---------------------------------------------------------------------------
 */

MODULE_SCOPE NSDictionary*
TkMacOSXNSFontAttributesForFont(
    Tk_Font tkfont)
{
    return tkfont ? ((MacFont *)tkfont)->nsAttributes : nil;
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXFontDescriptionForNSFontAndNSFontAttributes --
 *
 *	Get text description of a font specified by NSFont and attributes.
 *
 * Results:
 *	List object or NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE Tcl_Obj *
TkMacOSXFontDescriptionForNSFontAndNSFontAttributes(
    NSFont *nsFont,
    NSDictionary *nsAttributes)
{
    Tcl_Obj *objv[6];
    int i = 0;
    const char *familyName = [[nsFont familyName] UTF8String];

    if (nsFont && familyName) {
	NSFontTraitMask traits = [[NSFontManager sharedFontManager]
		traitsOfFont:nsFont];
	id underline = [nsAttributes objectForKey:
		NSUnderlineStyleAttributeName];
	id strikethrough = [nsAttributes objectForKey:
		NSStrikethroughStyleAttributeName];

	objv[i++] = Tcl_NewStringObj(familyName, TCL_INDEX_NONE);
	objv[i++] = Tcl_NewWideIntObj((Tcl_WideInt)floor([nsFont pointSize] * FACTOR + 0.5));
#define S(s)    Tcl_NewStringObj(STRINGIFY(s), (sizeof(STRINGIFY(s))-1))
	objv[i++] = (traits & NSBoldFontMask)	? S(bold)   : S(normal);
	objv[i++] = (traits & NSItalicFontMask)	? S(italic) : S(roman);
	if ([underline respondsToSelector:@selector(intValue)] &&
		([underline intValue] & (NSUnderlineStyleSingle |
		NSUnderlineStyleThick | NSUnderlineStyleDouble))) {
	    objv[i++] = S(underline);
	}
	if ([strikethrough respondsToSelector:@selector(intValue)] &&
		([strikethrough intValue] & (NSUnderlineStyleSingle |
		NSUnderlineStyleThick | NSUnderlineStyleDouble))) {
	    objv[i++] = S(overstrike);
	}
#undef S
    }
    return i ? Tcl_NewListObj(i, objv) : NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * TkMacOSXUseAntialiasedText --
 *
 *	Enables or disables application-wide use of antialiased text (where
 *	available). Sets up a linked Tcl global variable to allow disabling of
 *	antialiased text from Tcl.
 *
 *	The possible values for this variable are:
 *
 *	-1 - Use system default as configurable in "System Prefs" -> "General".
 *	 0 - Unconditionally disable antialiasing.
 *	 1 - Unconditionally enable antialiasing.
 *
 * Results:
 *
 *	TCL_OK.
 *
 * Side effects:
 *
 *	None.
 *
 *----------------------------------------------------------------------
 */

MODULE_SCOPE int
TkMacOSXUseAntialiasedText(
    Tcl_Interp * interp,	/* The Tcl interpreter to receive the
				 * variable.*/
    int enable)			/* Initial value. */
{
    static Boolean initialized = FALSE;

    if (!initialized) {
	initialized = TRUE;

	if (Tcl_CreateNamespace(interp, "::tk::mac", NULL, NULL) == NULL) {
	    Tcl_ResetResult(interp);
	}
	if (Tcl_LinkVar(interp, "::tk::mac::antialiasedtext",
		&antialiasedTextEnabled,
		TCL_LINK_INT) != TCL_OK) {
	    Tcl_ResetResult(interp);
	}
    }
    antialiasedTextEnabled = enable;
    return TCL_OK;
}

/*
 * Local Variables:
 * mode: objc
 * c-basic-offset: 4
 * fill-column: 79
 * coding: utf-8
 * End:
 */
